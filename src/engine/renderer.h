// renderer.h — Karo tabanlı, çok iş parçacıklı render sürücüsü.
//
// Renderer kameradan her piksel için ışın üretir, integrator'e (path tracer)
// verir ve sonucu Film'e biriktirir. İki çalışma biçimi var:
//   * render():            karo karo, her karonun TÜM örnekleri (CLI, turntable)
//   * renderSamplePass():  tüm görüntüye piksel başına 1 örnek (ilerlemeli viewport
//                          ve son render). Her pass görüntüyü biraz daha temizler.
// Pass'ler `cancel` bayrağıyla karo düzeyinde durdurulabilir.
#pragma once

#include "engine/scene.h"
#include "engine/render_settings.h"
#include "camera/camera.h"
#include "core/image/film.h"
#include "core/image/image.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

namespace photon {

class Sampler;

/// Üretim örnekleyicisi. Testler IndependentSampler kullanmaya devam eder.
std::unique_ptr<Sampler> createRenderSampler(int samplesPerPixel, uint64_t seed = 12345);

/// Gürültü gidericinin (OIDN) yardımcı kanalları. Her biri beauty ile aynı
/// alt-piksel (jitter) konumundan, ilk kesişimde toplanır; böylece kenarlar
/// beauty ile birebir hizalanır.
struct AovFilms {
    Film albedo;  ///< İlk yüzeyin taban rengi (ıskalayan ışında arka plan, ≤ 1).
    Film normal;  ///< İlk yüzeyin dünya uzayı normali (negatif bileşen olabilir).
    Film alpha;   ///< Kapsama: ışın geometriye çarptıysa 1, ıskaladıysa 0 (r kanalında).

    void resize(int w, int h) { albedo.resize(w, h); normal.resize(w, h); alpha.resize(w, h); }
};

class Renderer {
public:
    Renderer() = default;

    /// Senkron render: her karo için tüm örnekler. Ortalaması alınmış HDR görüntü
    /// döner; ayarlar isterse gürültüsü giderilmiş olur.
    Image render(const Scene& scene, const Camera& camera, const RenderSettings& settings,
                 const std::atomic<bool>* cancel = nullptr);

    /// Piksel başına birer örneklik pass'lerle samplesPerPixel'e kadar render.
    /// @param onPass Her pass sonrası film ve şimdiye dek örnek sayısıyla çağrılır.
    ///               false dönerse render durur (iptal).
    /// @param aovsOut Doluysa AOV'ler buraya da yazılır (şeffaf PNG için alfa).
    Image renderProgressive(const Scene& scene, const Camera& camera, const RenderSettings& settings,
                            const std::function<bool(const Film&, int)>& onPass,
                            AovFilms* aovsOut = nullptr);

    /// Var olan bir filme tek örneklik bir pass ekle.
    /// @return Pass tamamlandıysa true, iptal edildiyse false (film yarım kalır,
    ///         çağıran filmi atmalıdır).
    bool renderSamplePass(const Scene& scene, const Camera& camera, const RenderSettings& settings,
                          Film& accum, int passIndex, const std::atomic<bool>* cancel = nullptr,
                          AovFilms* aovs = nullptr);
};

} // namespace photon
