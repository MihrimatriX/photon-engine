// render_controller.h — Viewport'un arka planda sürekli çalışan ilerlemeli renderer'ı.
//
// UI thread'i hiçbir zaman bir render pass'ini beklemez. Akış:
//   1. UI sahneyi derler ve setScene() ile değişmez bir Scene (shared_ptr) verir.
//      Render thread'i eski sahneyi pass bitene kadar tutar; bellek güvenli kalır.
//   2. Kamera ya da ayar değişince restart(): birikim sıfırlanır.
//   3. Kullanıcı sürüklerken "etkileşimli" mod: görüntü 1/2–1/8 çözünürlükte
//      render edilir (çözünürlük merdiveni) ve her kare OIDN ile temizlenir.
//      200 ms hareketsizlikten sonra tam çözünürlüğe geçilir.
//   4. Her pass sonunda HDR görüntü çözülür, gerekirse gürültüsü giderilir, ton
//      eşlenir ve RGBA8 olarak yayınlanır; UI yalnız kısa bir kilitle alır.
// Malzeme gibi yerinde değiştirilen nesneler için lockScene()/edit(): mevcut
// pass iptal edilir ve thread boşa çıkana kadar (en fazla bir satır) beklenir.
//
// Render, seçilen RenderDevice üzerinde Etkileşimli modlu bir RenderSession ile yapılır
// (son render ve CLI ile aynı çekirdek ve aynı cihaz katmanı). Sahne cihaza yalnız
// değiştiğinde yüklenir (sürüm sayacı): kamera hareketi bir GPU cihazında yeniden yükleme
// gerektirmez.
#pragma once

#include "engine/render_session.h"
#include "engine/denoiser.h"
#include "engine/render_settings.h"
#include "core/image/imager.h"
#include "core/image/image.h"
#include "core/math/vec.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace photon {

/// Kamerayı tanımlayan değerler (en-boy oranı hariç; o render boyutundan gelir).
struct CameraParams {
    Vec3f eye{0, 0, 5};
    Vec3f target{0, 0, 0};
    float fovDeg = 35.0f;
    bool orthographic = false;
    float orthoHeight = 2.0f;
    float apertureRadius = 0.0f;
    float focusDistance = 5.0f;
    int apertureBlades = 0;
    float bladeRotationDeg = 0.0f;

    bool operator==(const CameraParams& o) const {
        return eye.x == o.eye.x && eye.y == o.eye.y && eye.z == o.eye.z && target.x == o.target.x &&
               target.y == o.target.y && target.z == o.target.z && fovDeg == o.fovDeg &&
               orthographic == o.orthographic && orthoHeight == o.orthoHeight &&
               apertureRadius == o.apertureRadius && focusDistance == o.focusDistance &&
               apertureBlades == o.apertureBlades && bladeRotationDeg == o.bladeRotationDeg;
    }
    bool operator!=(const CameraParams& o) const { return !(*this == o); }
};

/// Parametrelerden somut kamera nesnesi (pinhole / ince mercek / ortografik).
std::unique_ptr<Camera> makeCamera(const CameraParams& p, float aspect);

struct ViewportStats {
    int spp = 0;
    int targetSpp = 0;      ///< 0 = sınırsız
    int width = 0;
    int height = 0;
    double seconds = 0.0;   ///< Bu birikimin başlangıcından beri
    double passMs = 0.0;
    bool denoised = false;
    bool interactive = false;
    bool converged = false;
    std::string device;     ///< Render eden cihazın adı
    std::string deviceKind; ///< "CPU", "CUDA"... (rozet için kısa ad)
    std::string deviceNote; ///< İstenen cihaz açılamadıysa nedeni (CPU'ya düşüldü)
};

/// UI'a verilen hazır kare.
struct DisplayFrame {
    std::vector<uint8_t> rgba;  ///< Ton eşlenmiş, sRGB, satır 0 = üst
    int width = 0;
    int height = 0;
    bool hasAlpha = false;
};

class RenderController {
public:
    RenderController();
    ~RenderController();
    RenderController(const RenderController&) = delete;
    RenderController& operator=(const RenderController&) = delete;

    void start();
    void stop();

    void setScene(std::shared_ptr<const Scene> scene, bool interactive = false);
    void setCamera(const CameraParams& cam, bool interactive);
    /// Görüntüyü etkileyen alanlar değiştiyse yeniden başlatır.
    void setSettings(const RenderSettings& rs);
    /// Yalnız ekran dönüşümü (ton eşleme, pozlama): birikim korunur.
    void setDisplay(ToneMapOperator tmo, float exposureEV);
    /// Görüntüleyici (bloom, glare, renk) ekran görüntüsüne ton eşlemeden önce uygulanır.
    void setImager(const ImagerSettings& imager);
    void setViewport(int width, int height, float resolutionScale);
    void setDenoise(bool enabled);
    void setTargetSpp(int spp);  ///< 0 = sınırsız; artırmak birikimi sıfırlamaz
    void setPaused(bool paused);
    void setTransparentPreview(bool transparent);
    /// Render cihazı ("auto", "cpu", "cuda:0"). Açılamazsa CPU'ya düşülür; stats().deviceNote nedenini söyler.
    void setDevice(const std::string& id);
    void restart(bool interactive = false);

    /// Mevcut pass'i iptal eder ve sahne kilidini döndürür. Kilit tutulurken
    /// render thread'i sahneye dokunmaz; yerinde düzenleme güvenlidir.
    /// Kilit bırakılınca restart() çağrılmalı (edit() bunu yapar).
    std::unique_lock<std::mutex> lockScene();

    template <typename F>
    void edit(F&& fn, bool interactive = true) {
        {
            auto lock = lockScene();
            fn();
        }
        restart(interactive);
    }

    /// Yeni kare varsa @p out'a taşır.
    bool fetchFrame(DisplayFrame& out);
    ViewportStats stats() const;
    /// Son çözülmüş (ve gürültüsü giderilmiş) HDR görüntü ve kapsama (alfa).
    void lastImages(std::shared_ptr<const Image>& hdr, std::shared_ptr<const Image>& alpha) const;

private:
    using Clock = std::chrono::steady_clock;

    void threadMain();
    void publish(const Image& hdr, const Image* alpha, bool denoised);
    void retonemapLocked();

    std::thread m_thread;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;

    // ── m_mutex ile korunan istekler ──
    std::shared_ptr<const Scene> m_scene;
    CameraParams m_camera;
    RenderSettings m_settings;
    int m_vpW = 0;
    int m_vpH = 0;
    float m_resScale = 1.0f;
    bool m_denoise = true;
    int m_targetSpp = 0;
    bool m_paused = false;
    bool m_dirty = true;
    bool m_quit = false;
    bool m_retonemap = false;
    bool m_transparent = false;
    std::string m_deviceId = "auto";
    ToneMapOperator m_tmo = ToneMapOperator::PBRNeutral;
    float m_exposure = 0.0f;
    ImagerSettings m_imager;
    Clock::time_point m_lastInteraction{};

    // ── iptal / sahne kilidi ──
    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_passInteractive{false};
    std::mutex m_sceneMutex;
    std::atomic<uint64_t> m_sceneVersion{1}; ///< setScene ve yerinde düzenlemede artar

    // ── yalnız render thread'i ──
    std::unique_ptr<RenderDevice> m_device;
    std::string m_activeDeviceId;     ///< m_device hangi istekle açıldı
    std::unique_ptr<RenderSession> m_session;
    uint64_t m_uploadedVersion = 0;   ///< cihazdaki sahnenin sürümü
    const Scene* m_uploadedScene = nullptr;
    bool m_needBegin = true;          ///< yeni birikim (boyut/kamera/ayar değişti)
    int m_beginW = 0, m_beginH = 0;
    Denoiser m_denoiser;
    int m_spp = 0;
    bool m_converged = false;
    bool m_interactive = false;
    int m_ladder = 4;                 ///< Etkileşimli modda çözünürlük böleni
    Clock::time_point m_startTime{};

    // ── yayınlanan sonuçlar (m_frameMutex) ──
    mutable std::mutex m_frameMutex;
    DisplayFrame m_frame;
    bool m_frameNew = false;
    ViewportStats m_stats;
    std::shared_ptr<const Image> m_lastHdr;
    std::shared_ptr<const Image> m_lastAlpha;
};

} // namespace photon
