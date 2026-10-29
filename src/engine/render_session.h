// render_session.h — Render modu ve birikim oturumu: "nasıl" çalışıldığı.
//
// Aynı yol izleyici (PathTracer) her modda çalışır; modlar yalnız politikadır: kaç örnek,
// ne kadar süre, hangi çıktılar, ne zaman durulur. RTXPT'nin "Reference" ve "Realtime"
// modlarının aynı çekirdeği paylaşması gibi. Bugünkü modlar:
//
//   Interactive  viewport: piksel başına 1 örneklik pass'ler (her pass ekrana gelir, kamera
//                hareketinde ucuz yeniden başlatma), hedef örnek sayısına ya da sınırsız.
//   Final        son render, turntable, CLI: hedef örnek / süre sınırı / uyarlamalı yakınsama.
//                Örnekler partiler hâlinde verilir (karo önce: ~%7 hızlı); parti boyu ölçülen
//                hıza göre ~batchSeconds sürecek şekilde büyür, süre sınırı aşılmaz.
//
// Gerçek zamanlı bir mod (RTXPT benzeri: kare başına 1 örnek + zamansal yeniden kullanım +
// gerçek zamanlı gürültü giderici) GPU cihazıyla birlikte buraya eklenecek; oturumu
// kullanan kod (viewport, son render, CLI) değişmeyecek.
//
// Oturum bir RenderDevice üzerinde çalışır; cihazın sahnesi önceden setScene() ile verilmiş
// olmalıdır. Bir oturum tek iş parçacığından kullanılır.
#pragma once

#include "engine/denoiser.h"
#include "engine/render_device.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace photon {

enum class RenderMode { Interactive, Final };
const char* renderModeName(RenderMode mode);

struct SessionConfig {
    RenderMode mode = RenderMode::Final;
    int targetSpp = 64;         ///< Hedef örnek/piksel; 0 = sınırsız (yalnız süre ya da iptal durdurur)
    double timeLimit = 0.0;     ///< Saniye; 0 = sınır yok
    bool aovs = false;          ///< Albedo, normal, alfa (gürültü giderme kalitesi, şeffaf PNG)
    bool passes = false;        ///< Render geçişleri (çok katmanlı EXR)
    double batchSeconds = 0.5;  ///< Final: bir partinin hedef süresi (önizleme ve iptal temposu)
    int maxBatch = 64;          ///< Final: bir partideki en çok örnek
};

class RenderSession {
public:
    explicit RenderSession(RenderDevice& device) : m_device(device) {}

    /// Yeni birikim: filmler settings.width × height boyutunda sıfırlanır. Cihaz istenen bir
    /// çıktıyı desteklemiyorsa o çıktı kapatılır ve warnings()'e yazılır.
    void begin(std::shared_ptr<const Camera> camera, const RenderSettings& settings, const SessionConfig& config);

    /// Bir parti örnek ekler. false: iptal edildi (bu parti yarım; piksel ortalamaları yine
    /// geçerli) ya da oturum zaten bitmişti.
    bool advance(const std::atomic<bool>* cancel = nullptr);
    /// Hedef örnek sayısına, süre sınırına ya da (uyarlamalıda) yakınsamaya ulaşıldı mı?
    bool finished() const;

    /// Viewport'ta hedef yükseltilince birikim korunur.
    void setTargetSpp(int spp) { m_config.targetSpp = spp < 0 ? 0 : spp; }

    RenderMode mode() const { return m_config.mode; }
    const SessionConfig& config() const { return m_config; }
    const RenderSettings& settings() const { return m_settings; }
    int samples() const { return m_samples; }          ///< Tamamlanan örnek/piksel (parti sınırları)
    double averageSamples() const;                     ///< Gerçek ortalama (uyarlamalıda atlananlar düşülür)
    double seconds() const;                            ///< begin()'den beri geçen süre
    double lastBatchSeconds() const { return m_lastBatchSeconds; }
    bool converged() const { return m_converged; }
    const std::vector<std::string>& warnings() const { return m_warnings; }
    int width() const { return m_film.width(); }
    int height() const { return m_film.height(); }

    const Film& film() const { return m_film; }
    const AovFilms* aovs() const { return m_config.aovs ? &m_aovs : nullptr; }
    const RenderPassFilms* passes() const { return m_passes.get(); }

    /// Ham beauty (doğrusal HDR ortalama).
    Image resolve() const { return m_film.resolve(); }
    /// Beauty, AOV'ler açıksa onlarla birlikte gürültüsü giderilmiş (OIDN yoksa yumuşatma).
    Image resolveDenoised(Denoiser& denoiser, Denoiser::Quality quality) const;
    /// Kapsama (alfa, r kanalında); AOV'ler kapalıysa her piksel 1.
    Image resolveAlpha() const;

private:
    int nextBatch() const;

    using Clock = std::chrono::steady_clock;
    RenderDevice& m_device;
    std::shared_ptr<const Camera> m_camera;
    RenderSettings m_settings;
    SessionConfig m_config;
    Film m_film;
    AovFilms m_aovs;
    std::unique_ptr<RenderPassFilms> m_passes;
    std::vector<std::string> m_warnings;
    int m_samples = 0;
    uint64_t m_traced = 0;
    bool m_converged = false;
    double m_secondsPerSample = 0.0; ///< ölçülen; parti boyunu belirler
    double m_lastBatchSeconds = 0.0;
    Clock::time_point m_start{};
};

} // namespace photon
