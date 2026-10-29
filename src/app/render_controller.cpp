// render_controller.cpp — Viewport render thread'inin döngüsü ve yayınlama mantığı.
#include "app/render_controller.h"
#include <cstring>
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
    if (p.apertureRadius > 0.0f) {
        auto cam = std::make_unique<ThinLensCamera>(p.eye, p.target, up, p.fovDeg, aspect, p.apertureRadius,
                                                    std::max(1e-3f, p.focusDistance));
        cam->setApertureShape(p.apertureBlades, p.bladeRotationDeg);
        return cam;
    }
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
        ++m_sceneVersion;
    }
    restart(interactive);
}

void RenderController::setDevice(const std::string& id) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (id == m_deviceId) return;
        m_deviceId = id;
    }
    restart(false);
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
                  rs.adaptiveSampling != m_settings.adaptiveSampling ||
                  rs.noiseThreshold != m_settings.noiseThreshold || rs.clampIndirect != m_settings.clampIndirect ||
                  !(rs.filter == m_settings.filter);
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

void RenderController::setImager(const ImagerSettings& imager) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (std::memcmp(&imager, &m_imager, sizeof(ImagerSettings)) == 0) return;
        m_imager = imager;
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
    std::unique_lock<std::mutex> lock(m_sceneMutex);
    ++m_sceneVersion; // yerinde düzenleme: cihazdaki kopya (GPU) yenilenmeli
    return lock;
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
void RenderController::publish(const Image& hdrIn, const Image* alpha, bool denoised) {
    ToneMapOperator tmo;
    float exposure;
    bool transparent;
    ImagerSettings imager;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        tmo = m_tmo;
        exposure = m_exposure;
        transparent = m_transparent;
        imager = m_imager;
    }
    // Görüntüleyici HDR'nin kopyasına uygulanır (ham görüntü birikim ve dışa aktarım için kalır).
    Image imaged;
    const bool useImager = !imager.isIdentity();
    if (useImager) {
        imaged = hdrIn;
        applyImager(imaged, imager, exposure);
    }
    const Image& hdr = useImager ? imaged : hdrIn;
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
            return !m_converged && (m_targetSpp == 0 || m_spp < m_targetSpp);
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
            m_beginW = std::max(16, static_cast<int>(static_cast<float>(m_vpW) * m_resScale) / div);
            m_beginH = std::max(16, static_cast<int>(static_cast<float>(m_vpH) * m_resScale) / div);
            m_needBegin = true;
            m_spp = 0;
            m_converged = false;
            m_startTime = now;
            displayDenoised = false;
        } else if (!m_interactive && (m_converged || (m_targetSpp > 0 && m_spp >= m_targetSpp))) {
            continue;
        }

        std::shared_ptr<const Scene> scene = m_scene;
        const CameraParams camParams = m_camera;
        RenderSettings rs = m_settings;
        const bool wantDenoise = m_denoise && denoiseAvailable();
        const int target = m_targetSpp;
        const bool interactive = m_interactive;
        const std::string deviceId = m_deviceId;
        m_passInteractive = interactive;
        lk.unlock();

        // Cihaz: istek değiştiyse yeniden aç; açılamazsa CPU'ya düş (viewport hiç boş kalmasın).
        std::string deviceNote;
        if (!m_device || deviceId != m_activeDeviceId) {
            std::string err;
            std::unique_ptr<RenderDevice> dev = createDevice(deviceId, &err);
            if (!dev) {
                deviceNote = err;
                dev = createDevice("cpu");
            }
            m_session.reset(); // oturum eski cihaza başvurur: önce o bırakılır
            m_device = std::move(dev);
            m_activeDeviceId = deviceId;
            m_session = std::make_unique<RenderSession>(*m_device);
            m_uploadedScene = nullptr;
            m_needBegin = true;
            std::lock_guard<std::mutex> flk(m_frameMutex);
            m_stats.device = m_device->info().name;
            m_stats.deviceKind = deviceKindName(m_device->info().kind);
            m_stats.deviceNote = deviceNote;
        }

        const auto passStart = Clock::now();
        bool ok = false;
        try {
            std::lock_guard<std::mutex> sceneLock(m_sceneMutex);
            // Sahne yalnız değiştiğinde yüklenir (CPU'da bedava, GPU'da tampon kopyası).
            const uint64_t version = m_sceneVersion.load();
            if (m_uploadedScene != scene.get() || m_uploadedVersion != version) {
                m_device->setScene(scene);
                m_uploadedScene = scene.get();
                m_uploadedVersion = version;
            }
            if (m_needBegin) {
                rs.width = m_beginW;
                rs.height = m_beginH;
                const float aspect = static_cast<float>(rs.width) / static_cast<float>(std::max(1, rs.height));
                SessionConfig sc;
                sc.mode = RenderMode::Interactive;
                sc.targetSpp = target;
                sc.aovs = true; // gürültü giderme kılavuzları ve şeffaf önizleme
                m_session->begin(makeCamera(camParams, aspect), rs, sc);
                m_needBegin = false;
            } else {
                m_session->setTargetSpp(target);
            }
            ok = m_session->advance(&m_cancel);
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
        if (!ok) {
            // İptal: film yarım, bir sonraki turda yeniden kurulur. (Oturum bitmişse advance
            // false döner ama birikim geçerlidir; yakınsama bayrağı bekleme koşulunu kapatır.)
            if (m_session->finished()) m_converged = m_session->converged();
            continue;
        }

        m_spp = m_session->samples();
        m_converged = m_session->converged();
        if (interactive) {
            // Çözünürlük merdivenini ölçülen süreye göre ayarla.
            if (passMs > kInteractiveBudgetMs * 1.4 && m_ladder < 8) ++m_ladder;
            else if (passMs < kInteractiveBudgetMs * 0.35 && m_ladder > 2) --m_ladder;
        }

        // Gürültü giderme: etkileşimde her kare (hızlı kalite), sonra 2, 4, 8, ... örnekte
        // ve hedefe ulaşınca (yüksek kalite). Arada son temiz kare gösterilmeye devam eder.
        const bool reached = (target > 0 && m_spp >= target) || m_converged;
        const bool denoiseNow = wantDenoise && (interactive || isPowerOfTwo(m_spp) || reached);
        const bool publishNow = !wantDenoise || denoiseNow;

        if (publishNow) {
            const auto q = interactive ? Denoiser::Quality::Fast
                         : reached ? Denoiser::Quality::High : Denoiser::Quality::Balanced;
            auto hdr = std::make_shared<Image>(denoiseNow ? m_session->resolveDenoised(m_denoiser, q)
                                                          : m_session->resolve());
            auto alpha = std::make_shared<Image>(m_session->resolveAlpha());
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
        m_stats.width = m_session->width();
        m_stats.height = m_session->height();
        m_stats.seconds = std::chrono::duration<double>(Clock::now() - m_startTime).count();
        m_stats.passMs = passMs;
        m_stats.interactive = interactive;
        m_stats.converged = reached;
    }
}

} // namespace photon
