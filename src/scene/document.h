// document.h — Kullanıcının düzenlediği ışık ve ortam tanımları ("belge" verisi).
//
// Arayüz Light/EnvironmentLight nesnelerini doğrudan değiştirmez; bu sade,
// kopyalanabilir tanımları (LightDesc, EnvironmentDesc) düzenler. Her sahne
// derlemesinde tanımlardan yeni, değişmez render nesneleri üretilir. Böylece
// geri al, proje kaydetme ve render thread'i güvenliği kendiliğinden çözülür.
#pragma once

#include "engine/scene.h"
#include "core/image/image.h"
#include "core/math/aabb.h"
#include "lights/light.h"
#include "lights/environment_light.h"
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace photon {

struct LightDesc {
    enum class Type { Area, Directional, Point };
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
};

struct EnvironmentDesc {
    std::string hdrPath;                   ///< Boş = prosedürel stüdyo gökyüzü
    Color3f zenith{0.75f, 0.8f, 0.95f};
    Color3f horizon{0.55f, 0.55f, 0.58f};
    float rotationDeg = 0.0f;
    float intensity = 1.0f;
    Background background;
    bool groundEnabled = true;
    Color3f groundColor{0.42f};
    float groundRoughness = 0.55f;
    bool groundShadowOnly = false;         ///< ponytail: ileride gölge yakalayıcı
};

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
    void clear();
private:
    std::mutex m_mutex;
    std::map<std::string, std::shared_ptr<const Image>> m_images;
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

/// assets/studios/*.json dosyalarını oku.
std::vector<StudioPreset> loadStudioPresets(const std::string& dir);

/// Preset ışıklarını sahne sınırlarına göre yerleştir.
std::vector<LightDesc> placeStudioLights(const StudioPreset& preset, const AABB& sceneBounds);

} // namespace photon
