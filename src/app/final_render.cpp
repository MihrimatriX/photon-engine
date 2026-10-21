// final_render.cpp — Dosyaya son render ve turntable (360° kare dizisi).
//
// Son render, viewport'tan bağımsız bir iş parçacığında, malzemeleri kopyalanmış
// (izole) bir sahneyle çalışır: kullanıcı render sürerken düzenleme yapsa bile
// çıktı etkilenmez. Viewport bu sırada duraklatılır ki tüm çekirdekler render'a
// gitsin. Örnek sayısı ya da süre sınırına ulaşınca OIDN (yüksek kalite) ile
// gürültü giderilir ve seçilen biçimde kaydedilir.
#include "app/application.h"
#include "engine/denoiser.h"
#include "core/image/image_io.h"
#include "core/math/constants.h"
#include "core/platform/path.h"
#include "ui/widgets.h"

#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>

#ifdef _WIN32
#include <cstdlib>
#endif

namespace photon {

namespace fs = std::filesystem;

namespace {

// Varsayılan çıktı klasörü: Resimler/PhotonEngine.
std::string defaultRenderPath(int format) {
    fs::path base;
#ifdef _WIN32
    if (const wchar_t* home = _wgetenv(L"USERPROFILE")) base = fs::path(home) / "Pictures" / "PhotonEngine";
#else
    if (const char* home = std::getenv("HOME")) base = fs::path(home) / "Pictures" / "PhotonEngine";
#endif
    if (base.empty()) base = fs::current_path() / "renders";
    std::error_code ec;
    fs::create_directories(base, ec);
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char name[64];
    const char* ext = format == 1 ? "jpg" : format == 2 ? "exr" : "png";
    std::strftime(name, sizeof(name), "render_%Y%m%d_%H%M%S.", &tm);
    return pathToUtf8(base / (std::string(name) + ext));
}

bool saveByFormat(const Image& img, const Image* alpha, const std::string& path, int format,
                  ToneMapOperator tmo, float ev, bool transparent) {
    if (format == 2) return saveImageEXR(img, path);
    if (format == 1) return saveImageJPG(img, path, tmo, ev);
    if (transparent && alpha) return saveImagePNGAlpha(img, *alpha, path, tmo, ev);
    return saveImagePNG(img, path, tmo, ev);
}

} // namespace

void Application::cancelFinalRender() {
    m_s.finalJob.cancel = true;
}

void Application::joinFinalRender() {
    if (m_s.finalJob.thread.joinable()) m_s.finalJob.thread.join();
}

void Application::startFinalRender() {
    FinalRenderJob& job = m_s.finalJob;
    if (job.active) return;
    joinFinalRender();

    OutputSettings out = m_s.output;
    out.width = std::clamp(out.width, 16, 16384);
    out.height = std::clamp(out.height, 16, 16384);
    out.spp = std::max(1, out.spp);
    if (out.path.empty()) out.path = defaultRenderPath(out.format);
    m_s.output.path = out.path;

    auto scene = buildScene(true);
    const CameraParams cam = cameraParams();
    RenderSettings rs = m_s.settings;
    rs.width = out.width;
    rs.height = out.height;
    rs.samplesPerPixel = out.spp;
    rs.numThreads = 0;
    const bool transparent = m_s.environment.background.mode == Background::Mode::Transparent;
    const ToneMapOperator tmo = m_s.settings.tmo;
    const float ev = m_s.settings.exposure;

    job.cancel = false;
    job.spp = 0;
    job.targetSpp = out.spp;
    job.frame = 0;
    job.frames = 0;
    {
        std::lock_guard<std::mutex> lk(job.mutex);
        job.status = "Render ediliyor…";
        job.outputPath = out.path;
        job.preview.reset();
        job.previewNew = false;
        job.seconds = 0.0;
        job.succeeded = false;
    }
    job.active = true;

    job.thread = std::thread([this, scene, cam, rs, out, transparent, tmo, ev]() {
        FinalRenderJob& j = m_s.finalJob;
        using Clock = std::chrono::steady_clock;
        const auto t0 = Clock::now();
        try {
            auto camera = makeCamera(cam, static_cast<float>(rs.width) / static_cast<float>(rs.height));
            Renderer renderer;
            Film film(rs.width, rs.height);
            AovFilms aovs;
            aovs.resize(rs.width, rs.height);
            auto lastPreview = t0;
            int spp = 0;
            for (; spp < out.spp; ++spp) {
                if (j.cancel) break;
                renderer.renderSamplePass(*scene, *camera, rs, film, spp, &j.cancel, &aovs);
                if (j.cancel) break;
                j.spp = spp + 1;
                const double elapsed = std::chrono::duration<double>(Clock::now() - t0).count();
                // Önizleme yarım saniyede bir; büyük görüntüde her pass'te çözmek pahalı.
                if (std::chrono::duration<double>(Clock::now() - lastPreview).count() > 0.5 || spp == 0) {
                    auto prev = std::make_shared<Image>(film.resolve());
                    std::lock_guard<std::mutex> lk(j.mutex);
                    j.preview = prev;
                    j.previewNew = true;
                    j.seconds = elapsed;
                    lastPreview = Clock::now();
                }
                if (out.maxSeconds > 0.0f && elapsed >= out.maxSeconds) {
                    ++spp;
                    break;
                }
            }
            const bool userCancelled = j.cancel.load() && spp == 0;
            if (userCancelled) {
                std::lock_guard<std::mutex> lk(j.mutex);
                j.status = "İptal edildi";
            } else {
                Image img = film.resolve();
                Image alpha = aovs.alpha.resolve();
                if (out.denoise) {
                    {
                        std::lock_guard<std::mutex> lk(j.mutex);
                        j.status = "Gürültü gideriliyor (OIDN)…";
                    }
                    Image albedo = aovs.albedo.resolve();
                    Image normal = aovs.normal.resolve();
                    Denoiser denoiser;
                    denoiser.run(img, &albedo, &normal, Denoiser::Quality::High);
                }
                const bool ok = saveByFormat(img, &alpha, out.path, out.format, tmo, ev, transparent);
                auto finalImg = std::make_shared<Image>(std::move(img));
                std::lock_guard<std::mutex> lk(j.mutex);
                j.preview = finalImg;
                j.previewNew = true;
                j.seconds = std::chrono::duration<double>(Clock::now() - t0).count();
                j.succeeded = ok;
                j.status = ok ? (j.cancel ? "Durduruldu ve kaydedildi (" : "Tamamlandı (") + std::to_string(spp) +
                                    " örnek, " + ui::formatDuration(j.seconds) + ")"
                              : "Dosya yazılamadı: " + out.path;
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lk(j.mutex);
            j.status = std::string("Hata: ") + e.what();
        }
        j.active = false;
    });
}

// Turntable: kamera hedef etrafında eşit açılarla döner, her kare ayrı PNG.
// Kareler ffmpeg ile videoya çevrilebilir:
//   ffmpeg -framerate 30 -i frame_%04d.png -pix_fmt yuv420p turntable.mp4
void Application::startTurntable() {
    FinalRenderJob& job = m_s.finalJob;
    if (job.active) return;
    joinFinalRender();
    std::string dirPath = m_s.output.path.empty() ? defaultRenderPath(0) : m_s.output.path;
    fs::path dir = pathFromUtf8(dirPath);
    if (dir.has_extension()) dir = dir.parent_path() / (pathToUtf8(dir.stem()) + "_turntable");
    std::error_code ec;
    fs::create_directories(dir, ec);

    auto scene = buildScene(true);
    const OrbitCamera orbit = m_s.camera;
    RenderSettings rs = m_s.settings;
    rs.width = std::clamp(m_s.output.width, 16, 8192);
    rs.height = std::clamp(m_s.output.height, 16, 8192);
    rs.samplesPerPixel = std::max(1, m_s.output.turntableSpp);
    rs.denoiseEnabled = m_s.output.denoise;
    const int frames = std::max(4, m_s.output.turntableFrames);
    const ToneMapOperator tmo = m_s.settings.tmo;
    const float ev = m_s.settings.exposure;
    const CameraParams base = cameraParams();

    job.cancel = false;
    job.spp = 0;
    job.targetSpp = rs.samplesPerPixel;
    job.frame = 0;
    job.frames = frames;
    {
        std::lock_guard<std::mutex> lk(job.mutex);
        job.status = "Turntable render ediliyor…";
        job.outputPath = pathToUtf8(dir);
        job.preview.reset();
        job.succeeded = false;
    }
    job.active = true;
    job.thread = std::thread([this, scene, orbit, rs, frames, tmo, ev, base, dir]() {
        FinalRenderJob& j = m_s.finalJob;
        const auto t0 = std::chrono::steady_clock::now();
        try {
            Renderer renderer;
            for (int i = 0; i < frames && !j.cancel; ++i) {
                OrbitCamera c = orbit;
                c.phi = orbit.phi + TWO_PI * static_cast<float>(i) / static_cast<float>(frames);
                CameraParams p = base;
                p.eye = c.position();
                auto camera = makeCamera(p, static_cast<float>(rs.width) / static_cast<float>(rs.height));
                Image img = renderer.render(*scene, *camera, rs, &j.cancel);
                if (j.cancel) break;
                char name[32];
                std::snprintf(name, sizeof(name), "frame_%04d.png", i);
                saveImagePNG(img, pathToUtf8(dir / name), tmo, ev);
                j.frame = i + 1;
                auto prev = std::make_shared<Image>(std::move(img));
                std::lock_guard<std::mutex> lk(j.mutex);
                j.preview = prev;
                j.previewNew = true;
                j.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            }
            std::lock_guard<std::mutex> lk(j.mutex);
            j.succeeded = !j.cancel;
            j.status = j.cancel ? "Turntable durduruldu (" + std::to_string(j.frame.load()) + " kare)"
                                : "Turntable tamamlandı: " + std::to_string(frames) + " kare";
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lk(j.mutex);
            j.status = std::string("Hata: ") + e.what();
        }
        j.active = false;
    });
}

} // namespace photon
