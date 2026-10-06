// render_controller.cpp — Viewport render thread'inin döngüsü ve yayınlama mantığı.
#include "app/render_controller.h"
#include "camera/perspective_camera.h"
#include "camera/thin_lens_camera.h"
#include "camera/orthographic_camera.h"
#include "core/color/transfer.h"
#include "core/threading/parallel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace photon {

namespace {

constexpr auto kSettleTime = std::chrono::milliseconds(220);  // hareketsizlik → tam çözünürlük
constexpr double kInteractiveBudgetMs = 28.0;                 // etkileşimli pass hedefi (~35 fps)

bool isPowerOfTwo(int v) { return v > 0 && (v & (v - 1)) == 0; }

} // namespace

std::unique_ptr<Camera> makeCamera(const CameraParams& p, float aspect) {
    const Vec3f up(0, 1, 0);
    if (p.orthographic)
        return std::make_unique<OrthographicCamera>(p.eye, p.target, up, p.orthoHeight, aspect);
    if (p.apertureRadius > 0.0f)
        return std::make_unique<ThinLensCamera>(p.eye, p.target, up, p.fovDeg, aspect, p.apertureRadius,
                                                std::max(1e-3f, p.focusDistance));
    return std::make_unique<PerspectiveCamera>(p.eye, p.target, up, p.fovDeg, aspect);
}

RenderController::RenderController() = default;

RenderController::~RenderController() {
    stop();
}

void RenderController::start() {
    if (m_thread.joinable()) return;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_quit = false;
    }
    m_thread = std::thread([this] { threadMain(); });
}

void RenderController::stop() {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_quit = true;
        m_cancel = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

void RenderController::restart(bool interactive) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_dirty = true;
        if (interactive) m_lastInteraction = Clock::now();
        // Kısa (etkileşimli) bir pass yarıda kesilmez: bitsin, ekrana gelsin, sonra
        // en son kamerayla yeniden başlansın. Aksi hâlde sürekli sürüklemede hiçbir
        // kare tamamlanamazdı. Uzun tam çözünürlüklü pass hemen iptal edilir.
        if (!m_passInteractive.load()) m_cancel = true;
    }
    m_cv.notify_all();
}

void RenderController::setScene(std::shared_ptr<const Scene> scene, bool interactive) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_scene = std::move(scene);
    }
    restart(interactive);
}

void RenderController::setCamera(const CameraParams& cam, bool interactive) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (cam == m_camera) return;
        m_camera = cam;
    }
    restart(interactive);
}

void RenderController::setSettings(const RenderSettings& rs) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        changed = rs.maxBounces != m_settings.maxBounces || rs.aoStrength != m_settings.aoStrength ||
                  rs.shadowQuality != m_settings.shadowQuality ||
                  rs.adaptiveSampling != m_settings.adaptiveSampling;
        m_settings = rs;
    }
    if (changed) restart(true);
}

void RenderController::setDisplay(ToneMapOperator tmo, float exposureEV) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (tmo == m_tmo && exposureEV == m_exposure) return;
        m_tmo = tmo;
        m_exposure = exposureEV;
        m_retonemap = true;
    }
    m_cv.notify_all();
}

void RenderController::setViewport(int width, int height, float resolutionScale) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (width == m_vpW && height == m_vpH && resolutionScale == m_resScale) return;
        m_vpW = width;
        m_vpH = height;
        m_resScale = resolutionScale;
    }
    restart(true);
}

void RenderController::setDenoise(bool enabled) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (enabled == m_denoise) return;
        m_denoise = enabled;
    }
    restart(false);
}

void RenderController::setTargetSpp(int spp) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_targetSpp = std::max(0, spp);
    }
    m_cv.notify_all();
}

void RenderController::setPaused(bool paused) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (paused == m_paused) return;
        m_paused = paused;
        if (paused) m_cancel = true;
        else m_dirty = true; // yarım kalan pass'in filmi atılır
    }
    m_cv.notify_all();
}

void RenderController::setTransparentPreview(bool transparent) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (transparent == m_transparent) return;
        m_transparent = transparent;
        m_retonemap = true;
    }
    m_cv.notify_all();
}

std::unique_lock<std::mutex> RenderController::lockScene() {
    if (!m_passInteractive.load()) m_cancel = true;
    return std::unique_lock<std::mutex>(m_sceneMutex);
}

bool RenderController::fetchFrame(DisplayFrame& out) {
    std::lock_guard<std::mutex> lk(m_frameMutex);
    if (!m_frameNew) return false;
    out = std::move(m_frame);
    m_frame = DisplayFrame{};
    m_frameNew = false;
    return true;
}

ViewportStats RenderController::stats() const {
    std::lock_guard<std::mutex> lk(m_frameMutex);
    return m_stats;
}

void RenderController::lastImages(std::shared_ptr<const Image>& hdr, std::shared_ptr<const Image>& alpha) const {
    std::lock_guard<std::mutex> lk(m_frameMutex);
    hdr = m_lastHdr;
    alpha = m_lastAlpha;
}

// HDR → ekran: pozlama, ton eğrisi, sRGB kodlama, titreşimli (dither) 8 bit.
// Satırlar paralel işlenir; 1 MP görüntü birkaç milisaniye sürer.
void RenderController::publish(const Image& hdr, const Image* alpha, bool denoised) {
    ToneMapOperator tmo;
    float exposure;
    bool transparent;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        tmo = m_tmo;
        exposure = m_exposure;
        transparent = m_transparent;
    }
    DisplayFrame frame;
    frame.width = hdr.width();
    frame.height = hdr.height();
    frame.hasAlpha = transparent && alpha;
    frame.rgba.resize(static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * 4);
    parallelFor2D(frame.width, frame.height, [&](int x0, int x1, int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                Color3f c = hdr.getPixel(x, y);
                float a = 1.0f;
                if (frame.hasAlpha) {
                    a = std::clamp(alpha->getPixel(x, y).r, 0.0f, 1.0f);
                    c = a > 1e-4f ? c / a : Color3f::black();  // önceden çarpılmış → düz alfa
                }
                const Color3f d = toneMap(c, tmo, exposure);
                const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(frame.width) + static_cast<size_t>(x)) * 4;
                frame.rgba[i + 0] = quantizeUnorm8(d.r, tpdfDither(x, y, 0));
                frame.rgba[i + 1] = quantizeUnorm8(d.g, tpdfDither(x, y, 1));
                frame.rgba[i + 2] = quantizeUnorm8(d.b, tpdfDither(x, y, 2));
                frame.rgba[i + 3] = quantizeUnorm8(a);
            }
        }
    }, 64);

    std::lock_guard<std::mutex> lk(m_frameMutex);
    m_frame = std::move(frame);
    m_frameNew = true;
    m_stats.denoised = denoised;
}

void RenderController::threadMain() {
    std::shared_ptr<const Image> displayHdr;
    std::shared_ptr<const Image> displayAlpha;
    bool displayDenoised = false;

    while (true) {
        std::unique_lock<std::mutex> lk(m_mutex);
        auto workPending = [&] {
            if (m_quit || m_retonemap) return true;
            if (m_paused || !m_scene || m_vpW <= 0 || m_vpH <= 0) return false;
            if (m_dirty) return true;
            if (m_interactive) return true; // tam çözünürlüğe geçişi kontrol et / devam et
            return m_targetSpp == 0 || m_spp < m_targetSpp;
        };
        m_cv.wait_for(lk, std::chrono::milliseconds(60), workPending);
        if (m_quit) break;

        if (m_retonemap) {
            m_retonemap = false;
            if (displayHdr && !m_dirty) {
                lk.unlock();
                publish(*displayHdr, displayAlpha.get(), displayDenoised);
                continue;
            }
        }
        if (!workPending()) continue;

        const auto now = Clock::now();
        // Etkileşim bitti mi? Düşük çözünürlükteyken yeterince beklendiyse tam çözünürlüğe geç.
        if (m_interactive && now - m_lastInteraction > kSettleTime) m_dirty = true;

        if (m_dirty) {
            m_dirty = false;
            m_cancel = false;
            m_interactive = now - m_lastInteraction < kSettleTime;
            const int div = m_interactive ? std::max(1, m_ladder) : 1;
            const int w = std::max(16, static_cast<int>(static_cast<float>(m_vpW) * m_resScale) / div);
            const int h = std::max(16, static_cast<int>(static_cast<float>(m_vpH) * m_resScale) / div);
            m_film.resize(w, h);
            m_aovs.resize(w, h);
            m_spp = 0;
            m_startTime = now;
            displayDenoised = false;
        } else if (!m_interactive && m_targetSpp > 0 && m_spp >= m_targetSpp) {
            continue;
        }

        std::shared_ptr<const Scene> scene = m_scene;
        const CameraParams camParams = m_camera;
        RenderSettings rs = m_settings;
        const bool wantDenoise = m_denoise && denoiseAvailable();
        const int target = m_targetSpp;
        const bool interactive = m_interactive;
        m_passInteractive = interactive;
        lk.unlock();

        rs.width = m_film.width();
        rs.height = m_film.height();
        rs.samplesPerPixel = target > 0 ? target : 1 << 20;
        const float aspect = static_cast<float>(rs.width) / static_cast<float>(std::max(1, rs.height));
        std::unique_ptr<Camera> camera = makeCamera(camParams, aspect);

        const auto passStart = Clock::now();
        bool ok = false;
        try {
            std::lock_guard<std::mutex> sceneLock(m_sceneMutex);
            ok = m_renderer.renderSamplePass(*scene, *camera, rs, m_film, m_spp, &m_cancel, &m_aovs);
        } catch (const std::exception& e) {
            // Bir karodaki hata thread'i öldürmesin: birikimi at, bir sonraki
            // değişiklikte yeniden dene (F1.8). Hata konsola yazılır.
            std::fprintf(stderr, "Viewport render hatası: %s\n", e.what());
            std::lock_guard<std::mutex> relk(m_mutex);
            m_dirty = false;
            m_passInteractive = false;
            m_spp = m_targetSpp > 0 ? m_targetSpp : m_spp;
            continue;
        }
        m_passInteractive = false;
        const double passMs = std::chrono::duration<double, std::milli>(Clock::now() - passStart).count();
        if (!ok) continue; // iptal: film yarım, bir sonraki turda yeniden kurulur

        ++m_spp;
        if (interactive) {
            // Çözünürlük merdivenini ölçülen süreye göre ayarla.
            if (passMs > kInteractiveBudgetMs * 1.4 && m_ladder < 8) ++m_ladder;
            else if (passMs < kInteractiveBudgetMs * 0.35 && m_ladder > 2) --m_ladder;
        }

        // Gürültü giderme: etkileşimde her kare (hızlı kalite), sonra 2, 4, 8, ... örnekte
        // ve hedefe ulaşınca (yüksek kalite). Arada son temiz kare gösterilmeye devam eder.
        const bool reached = target > 0 && m_spp >= target;
        const bool denoiseNow = wantDenoise && (interactive || isPowerOfTwo(m_spp) || reached);
        const bool publishNow = !wantDenoise || denoiseNow;

        if (publishNow) {
            auto hdr = std::make_shared<Image>(m_film.resolve());
            auto alpha = std::make_shared<Image>(m_aovs.alpha.resolve());
            if (denoiseNow) {
                Image albedo = m_aovs.albedo.resolve();
                Image normal = m_aovs.normal.resolve();
                const auto q = interactive ? Denoiser::Quality::Fast
                             : reached ? Denoiser::Quality::High : Denoiser::Quality::Balanced;
                m_denoiser.run(*hdr, &albedo, &normal, q);
            }
            displayHdr = hdr;
            displayAlpha = alpha;
            displayDenoised = denoiseNow;
            {
                std::lock_guard<std::mutex> flk(m_frameMutex);
                m_lastHdr = hdr;
                m_lastAlpha = alpha;
            }
            publish(*hdr, alpha.get(), denoiseNow);
        }

        std::lock_guard<std::mutex> flk(m_frameMutex);
        m_stats.spp = m_spp;
        m_stats.targetSpp = target;
        m_stats.width = m_film.width();
        m_stats.height = m_film.height();
        m_stats.seconds = std::chrono::duration<double>(Clock::now() - m_startTime).count();
        m_stats.passMs = passMs;
        m_stats.interactive = interactive;
        m_stats.converged = reached;
    }
}

} // namespace photon
