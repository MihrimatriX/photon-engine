// scene.h — Render'a hazır, "düzleştirilmiş" sahne: dünya uzayına bake edilmiş
// geometri, ışıklar, ortam ışığı ve tek bir BVH hızlandırma yapısı.
//
// Düzenlenebilir sahne ağacı (SceneGraph) her değişiklikte bu yapıya derlenir.
// Render thread'i yalnız bu nesneyi okur. Scene, referans verdiği malzemeleri
// shared_ptr ile tutar; böylece UI bir malzemeyi silse bile render sürerken
// bellek serbest kalmaz.
#pragma once

#include "geometry/bvh.h"
#include "engine/embree_accel.h"
#include "lights/light.h"
#include "lights/environment_light.h"
#include "materials/material.h"
#include <vector>
#include <memory>
#include <unordered_map>

namespace photon {

/// Kameradan doğrudan görülen arka plan (yansımalar her zaman ortamı görür).
struct Background {
    enum class Mode { Environment, Color, Transparent };
    Mode mode = Mode::Environment;
    Color3f color{0.18f};
};

/// @brief Render edilebilir tüm öğeleri içeren sahne konteyneri.
class Scene {
public:
    Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    /// Geometrik şekil ekle. Emisyonlu malzemeli mesh'ler otomatik olarak
    /// MeshLight olarak da kaydedilir (doğrudan ışık örneklemesi için).
    void addShape(std::shared_ptr<Shape> shape);

    /// Işık kaynağı ekle. Alan ışıkları BVH'ye görünür bir dörtgen olarak da girer.
    /// BVH'yi KURMAZ: tüm şekil ve ışıklar eklendikten sonra buildAccelerator() çağrılır.
    void addLight(std::shared_ptr<Light> light);

    /// Ortam haritası / arka plan ışığı.
    void setEnvironment(std::shared_ptr<EnvironmentLight> envLight);

    void setBackground(const Background& bg) { m_background = bg; }
    const Background& background() const { return m_background; }

    /// Malzemeyi sahne ömrü boyunca canlı tut (mesh'ler ham işaretçi saklar).
    void retainMaterial(std::shared_ptr<const Material> material);

    /// Eklenen tüm şekiller üzerinde BVH'yi kur. Derlemenin son adımı.
    void buildAccelerator();

    /// Embree varken bile kendi BVH'mizi kullan (testler ve karşılaştırma için).
    static void setPreferOwnBvh(bool own);

    /// Tüm içeriği temizle.
    void reset();

    /// En yakın kesişim.
    bool intersect(Ray& ray, SurfaceInteraction& isect) const;

    /// Gölge ışını: [tMin, tMax] aralığında herhangi bir engel var mı?
    bool intersectAny(const Ray& ray) const;

    const std::vector<const Light*>& lights() const { return m_lightPtrs; }
    const EnvironmentLight* environment() const { return m_environment.get(); }
    size_t shapeCount() const { return m_shapes.size(); }

    /// Tüm geometrinin dünya uzayındaki sınırlayıcı kutusu.
    AABB bounds() const;

    /// Bake edilmiş şekli kaynak sahne düğümünün kimliğiyle etiketle (tıklayarak seçim).
    void tagShape(const void* shape, uint64_t nodeUid) { m_shapeOwner[shape] = nodeUid; }
    /// Kesişimdeki nesnenin düğüm kimliği; bilinmiyorsa 0.
    uint64_t nodeUidOf(const SurfaceInteraction& isect) const {
        auto it = m_shapeOwner.find(isect.hitObject);
        return it == m_shapeOwner.end() ? 0 : it->second;
    }

private:
    // İlk bildirilen son yok edilir: malzemeler, onlara işaret eden üçgenlerden uzun yaşar.
    std::vector<std::shared_ptr<const Material>> m_materials;
    std::vector<std::shared_ptr<Material>> m_emissiveMaterials;
    std::vector<std::shared_ptr<Shape>> m_shapes;
    std::vector<std::shared_ptr<Light>> m_lights;
    std::shared_ptr<EnvironmentLight> m_environment;
    Background m_background;

    BVH m_bvh;                            ///< Kendi BVH'miz (Embree yoksa / testlerde)
    std::unique_ptr<EmbreeAccel> m_embree; ///< Varsa üretim hızlandırıcısı

    // Integrator'ün hızlı erişimi için ham işaretçi önbelleği.
    std::vector<const Light*> m_lightPtrs;
    std::unordered_map<const void*, uint64_t> m_shapeOwner;
};

} // namespace photon
