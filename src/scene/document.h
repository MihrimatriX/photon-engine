// document.h — Kullanıcının düzenlediği ışık ve ortam tanımları ("belge" verisi).
//
// Arayüz Light/EnvironmentLight nesnelerini doğrudan değiştirmez; bu sade,
// kopyalanabilir tanımları (LightDesc, EnvironmentDesc) düzenler. Her sahne
// derlemesinde tanımlardan yeni, değişmez render nesneleri üretilir. Böylece
// geri al, proje kaydetme ve render thread'i güvenliği kendiliğinden çözülür.
#pragma once

#include "engine/scene.h"
#include "scene/scene_graph.h"
#include "core/image/image.h"
#include "core/math/aabb.h"
#include "lights/light.h"
#include "lights/environment_light.h"
#include "lights/sky_model.h"
#include "core/image/imager.h"
#include <nlohmann/json_fwd.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace photon {

struct LightDesc {
    enum class Type { Area, Directional, Point, Spot };
    Type type = Type::Area;
    std::string name = "Işık";
    bool enabled = true;
    Color3f color{1.0f};
    /// Alan: radyans; yönlü: ışınım (irradiance); nokta: yoğunluk. Hepsi color ile çarpılır.
    float intensity = 10.0f;
    Vec3f position{0.0f, 3.0f, 0.0f};  ///< Alan/nokta: merkez
    Vec3f target{0.0f, 0.0f, 0.0f};    ///< Alan: dörtgenin baktığı nokta
    float width = 1.0f;                ///< Alan: dörtgen boyutları
    float height = 1.0f;
    float azimuthDeg = 45.0f;          ///< Yönlü: ışığın GELDİĞİ yön (yatay açı)
    float elevationDeg = 45.0f;        ///< Yönlü: ufkun üstündeki açı
    /// Alan: kamera dörtgeni doğrudan görsün mü? Ürün çekiminde softbox'lar genelde
    /// görünmez; yansımalarda ve aydınlatmada her durumda vardır.
    bool visibleToCamera = false;
    float coneAngleDeg = 45.0f;        ///< Spot: koninin tam açısı (≥ 180 = yalnız IES)
    float coneBlend = 0.15f;           ///< Spot: kenar yumuşaklığı (0 keskin, 1 tam yumuşak)
    std::string iesPath;               ///< Spot: IES fotometrik profili (boş = yok)
};

struct EnvironmentDesc {
    /// Image: HDRI dosyası ya da prosedürel stüdyo gradyanı. Sky: fiziksel gök + güneş.
    enum class Kind { Image, Sky };
    Kind kind = Kind::Image;
    std::string hdrPath;                   ///< Boş = prosedürel stüdyo gökyüzü
    Color3f zenith{0.75f, 0.8f, 0.95f};
    Color3f horizon{0.55f, 0.55f, 0.58f};
    float rotationDeg = 0.0f;
    float intensity = 1.0f;
    Background background;
    bool groundEnabled = true;
    Color3f groundColor{0.42f};
    float groundRoughness = 0.55f;
    /// Gölge yakalayıcı: kamera zemini görmez, onun yerine arka planı görür; zemine düşen
    /// gölgeler arka planın üstüne karartma olarak (şeffaf arka planda alfa olarak) biner.
    /// Ürünü bir fotoğrafa ya da web sayfasına yerleştirmek için (KeyShot "ground shadows").
    bool groundShadowOnly = false;

    // ── Fiziksel gök (kind = Sky) ──
    SkyParams sky;
    /// Konum ve saatten güneş konumu (true) ya da elle yükseklik/azimut (false).
    bool skyFromLocation = false;
    float latitude = 41.01f;               ///< İstanbul
    float longitude = 28.98f;
    int dayOfYear = 172;                   ///< 21 Haziran
    float hour = 14.0f;                    ///< yerel saat
    float utcOffset = 3.0f;
    float northOffsetDeg = 0.0f;           ///< sahnenin kuzeyi −Z'den bu kadar döndürülmüş
};

/// Konum/saat seçiliyse güneşin yükseklik ve sahne azimutunu hesaplayıp sky'a yazar.
void resolveSunPosition(EnvironmentDesc& env);

/// Görüntüleyici ayarları ↔ proje dosyasının "render.imager" nesnesi.
nlohmann::json imagerToJson(const ImagerSettings& s);
ImagerSettings imagerFromJson(const nlohmann::json& j);

/// LightDesc listesinden render ışıkları (yalnız etkin olanlar).
std::vector<std::shared_ptr<Light>> buildLights(const std::vector<LightDesc>& descs);

/// Yönlü ışığın ışığın GİTTİĞİ yön vektörü (azimut/yükseklikten).
Vec3f directionalTravelDir(const LightDesc& d);

/// Tek renkli/geçişli prosedürel gökyüzü + iki "pencere" (krom yansımaları için).
std::shared_ptr<const Image> makeProceduralSky(const Color3f& zenith, const Color3f& horizon);

/// HDR dosyalarını bir kez yükleyip paylaşan önbellek (UI ve küçük resimler ortak kullanır).
class EnvironmentCache {
public:
    /// Dosyayı yükler (ya da önbellekten verir). Başarısızsa nullptr.
    std::shared_ptr<const Image> get(const std::string& path);
    /// Fiziksel gök (aynı parametrelerle yeniden hesaplanmaz; son birkaç sonuç tutulur).
    std::shared_ptr<const SkyResult> sky(const SkyParams& params);
    void clear();
private:
    std::mutex m_mutex;
    std::map<std::string, std::shared_ptr<const Image>> m_images;
    std::vector<std::pair<std::string, std::shared_ptr<const SkyResult>>> m_skies;
};

/// Tanımdan ortam ışığı.
std::shared_ptr<EnvironmentLight> buildEnvironment(const EnvironmentDesc& desc, EnvironmentCache& cache);

/// Stüdyo preset'i: ortam + sahne boyutuna göre ölçeklenen ışık düzeni.
struct StudioPreset {
    std::string id;
    std::string name;
    std::string description;
    std::string environment;      ///< assets/environments içindeki dosya adı (boş = değiştirme)
    float rotationDeg = 0.0f;
    float intensity = 1.0f;
    float exposure = 0.0f;
    bool hasExposure = false;
    struct Rig {
        LightDesc::Type type = LightDesc::Type::Area;
        std::string name;
        float azimuthDeg = 0.0f;
        float elevationDeg = 45.0f;
        float distance = 2.5f;    ///< sahne yarıçapının katı
        float size = 1.0f;        ///< sahne yarıçapının katı
        float intensity = 10.0f;
        Color3f color{1.0f};
    };
    std::vector<Rig> lights;
};

/// Zemin sahne ağacında bir düğüm değildir; seçimde bu özel kimlikle temsil edilir.
constexpr uint64_t kGroundNodeUid = ~0ull;

/// Belgeden (ağaç + ışıklar + ortam) render'a hazır sahne: geometri, otomatik
/// zemin, ışıklar, ortam, arka plan ve BVH. Uygulama ve CLI aynı yolu kullanır.
/// @param isolate Malzemeleri kopyala (son render; kullanıcı düzenlemesinden bağımsız).
/// @param clay    Kil modu: tüm parçalar aynı nötr mat griyle (ışık ve formu görmek için).
std::shared_ptr<Scene> buildRenderScene(const SceneGraph& graph, const std::vector<LightDesc>& lights,
                                        const EnvironmentDesc& env, EnvironmentCache& cache, bool isolate,
                                        bool clay = false);

/// assets/studios/*.json dosyalarını oku.
std::vector<StudioPreset> loadStudioPresets(const std::string& dir);

/// Preset ışıklarını sahne sınırlarına göre yerleştir.
/// @p azimuthOffsetDeg: preset açıları kameraya göredir; kameranın yatay açısı eklenir
/// ki "ana ışık kameranın 50° solunda" her bakış açısında doğru kalsın.
std::vector<LightDesc> placeStudioLights(const StudioPreset& preset, const AABB& sceneBounds,
                                         float azimuthOffsetDeg = 0.0f);

} // namespace photon
