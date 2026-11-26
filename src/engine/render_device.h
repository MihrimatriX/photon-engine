// render_device.h — Render cihazı soyutlaması (F9.7): "nerede" çalışıldığı.
//
// Motor bir sahneyi bir cihazda render eder. Bugün tek gerçek cihaz CPU'dur (Embree +
// iş parçacığı havuzu). GPU arka uçları (CUDA: GTX 1080 sm_61; OptiX / Vulkan RT /
// D3D12 DXR: RT çekirdekli kartlar) aynı arayüzü uygulayacak; render döngüleri
// (RenderSession), arayüz ve CLI cihazın türünü bilmez.
//
// Arayüzün şekli Cycles'ın Device'ı ve PBRT-v4'ün CPU/GPU ayrımına benzer:
//   setScene(): sahne cihaza yüklenir. CPU paylaşılan işaretçiyi tutar; bir GPU cihazı
//               düz tamponlara paketleyip kopyalar. Yalnız sahne değişince çağrılır,
//               kamera hareketi yeni yükleme gerektirmez.
//   render():   her piksele bir örnek aralığı eklenir, sonuç çağıranın filmlerine biriktirilir.
//
// Cihazlar enumerateDevices() ile listelenir: CPU her zaman, NVIDIA GPU'lar CUDA sürücüsü
// varsa (sürücü dinamik yüklenir, derlemede bağımlılık yok). Arka ucu bu derlemede olmayan
// cihazlar "kullanılamaz" olarak, nedeniyle birlikte listelenir: kullanıcı makinesinde
// neyin mümkün olduğunu görür (ör. GTX 1080: RT çekirdeği yok → OptiX/DXR yolu kapalı).
#pragma once

#include "engine/renderer.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace photon {

enum class DeviceKind { Cpu, Cuda, Optix, Vulkan, D3D12, Hybrid };
const char* deviceKindName(DeviceKind kind);

/// Artımlı sahne güncelleme türü (F3.8): GPU'da yalnız değişen tamponlar yeniden yüklenir.
enum class SceneUpdateKind : uint8_t { Full = 0, Materials, Lights, Transforms };

/// Bir cihazın kimliği ve donanım bilgisi.
struct DeviceInfo {
    std::string id;               ///< "cpu", "vulkan:0", "hybrid", "cuda:0"
    DeviceKind kind = DeviceKind::Cpu;
    std::string name;             ///< "Intel Core i7-6700K", "NVIDIA GeForce RTX 4090"
    int threads = 0;              ///< CPU: donanım iş parçacığı sayısı
    uint64_t memoryBytes = 0;     ///< GPU: toplam bellek
    int computeMajor = 0;         ///< CUDA / Vulkan sürüm ana numarası
    int computeMinor = 0;
    bool hardwareRayTracing = false; ///< Donanım RT çekirdekleri (VK_KHR_ray_query / RT çekirdekleri)
    bool usable = false;          ///< Bu derlemede bu cihaz için bir arka uç var mı
    std::string note;             ///< Kullanılamıyorsa nedeni; kullanılabiliyorsa kısa açıklama
};

/// Bir arka ucun desteklediği özellikler. Yeni bir GPU arka ucu her şeyi ilk günden
/// desteklemeyebilir; oturum ve arayüz bunlara bakıp istenmeyen çıktıyı kapatır ve uyarır.
struct DeviceFeatures {
    bool aovs = false;             ///< albedo / normal / alfa (gürültü giderme, şeffaf PNG)
    bool lightPasses = false;      ///< render geçişleri (lob × doğrudan/dolaylı, veri geçişleri)
    bool adaptiveSampling = false; ///< gürültü eşikli uyarlamalı örnekleme
};

/// Cihazın biriktireceği hedefler. beauty zorunlu; diğerleri isteğe bağlı.
struct RenderTargets {
    Film* beauty = nullptr;
    AovFilms* aovs = nullptr;
    RenderPassFilms* passes = nullptr;
};

class RenderDevice {
public:
    virtual ~RenderDevice() = default;

    virtual const DeviceInfo& info() const = 0;
    virtual DeviceFeatures features() const = 0;

    /// Sahneyi cihaza yükler (ya da günceller). Sahne değişmez kabul edilir; yerinde
    /// düzenlemeden sonra (malzeme) yeniden çağrılmalıdır ki GPU kopyası da güncellensin.
    virtual void setScene(std::shared_ptr<const Scene> scene) = 0;
    /// Artımlı sahne güncellemesi: varsayılanı setScene() çağırır; GPU cihazları yalnız değişen
    /// tamponu (Materials / Lights / Transforms TLAS) yeniden yükler.
    virtual void updateScene(std::shared_ptr<const Scene> scene, SceneUpdateKind /*kind*/) {
        setScene(std::move(scene));
    }
    virtual bool hasScene() const = 0;

    /// Her piksele [firstSample, firstSample + count) örnekleri ekler. Hedeflerin boyutu
    /// beauty filminden alınır. İptal edilirse completed = false.
    virtual SampleBatchResult render(const Camera& camera, const RenderSettings& settings, RenderTargets& targets,
                                     int firstSample, int count, const std::atomic<bool>* cancel = nullptr) = 0;
};

/// Bu makinedeki cihazlar: ilk eleman her zaman CPU'dur. GPU sorgusu ilk çağrıda bir kez
/// yapılır ve önbelleklenir.
const std::vector<DeviceInfo>& enumerateDevices();

/// @p id: "auto" (en iyi kullanılabilir donanım cihazı), "cpu", "vulkan:0", "hybrid" ya da
/// enumerateDevices()'daki bir kimlik. Yoksa nullptr döner ve @p error nedenini yazar.
std::unique_ptr<RenderDevice> createDevice(const std::string& id = "auto", std::string* error = nullptr);

} // namespace photon
