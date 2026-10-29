// renderer.cpp — Piksel örnekleme, karo dağıtımı, iptal ve gürültü giderme akışı.
#include "engine/renderer.h"
#include "engine/denoiser.h"
#include "core/threading/parallel.h"
#include "samplers/sobol_sampler.h"
#include "integrators/path_tracer.h"
#include "materials/disney.h"
#include "materials/dielectric.h"
#include "materials/lambertian.h"
#include "materials/mirror.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

namespace photon {

namespace {

ThreadPool* makePoolOverride(const RenderSettings& settings, std::unique_ptr<ThreadPool>& owned) {
    if (settings.numThreads <= 0) return nullptr;
    owned = std::make_unique<ThreadPool>(static_cast<size_t>(settings.numThreads));
    return owned.get();
}

// Raster (x, y)'de y = 0 ÜSTTE; kameranın ekran uzayında v = 0 ALTTA.
Vec2f rasterToScreen(float x, float y, int width, int height) {
    return Vec2f(x / static_cast<float>(width), 1.0f - y / static_cast<float>(height));
}

Color3f clamp01(const Color3f& c) {
    return Color3f(std::clamp(c.r, 0.0f, 1.0f), std::clamp(c.g, 0.0f, 1.0f), std::clamp(c.b, 0.0f, 1.0f));
}

// OIDN'in "albedo" kanalı: yüzeyin dokusuz/ışıksız taban rengi. Ayna ve cam
// gibi delta yüzeylerde yansıtma rengi verilir.
Color3f primaryAlbedo(const SurfaceInteraction& si) {
    if (!si.material) return Color3f::black();
    if (const auto* disney = dynamic_cast<const DisneyMaterial*>(si.material))
        return disney->resolve(si).baseColor;
    if (const auto* lambert = dynamic_cast<const Lambertian*>(si.material))
        return lambert->albedo();
    if (const auto* glass = dynamic_cast<const Dielectric*>(si.material))
        return glass->tint();
    if (const auto* mirror = dynamic_cast<const Mirror*>(si.material))
        return mirror->reflectance();
    return Color3f(0.5f);
}

struct PixelSample {
    Color3f L;
    Color3f albedo;
    Color3f normal;
    float alpha = 1.0f;
};

// Tek bir alt-piksel örneği. AOV istendiğinde ya da arka plan modu ortam değilse
// path tracer ilk kesişimi bize bildirir (ayrı bir birincil ışın atılmaz): ıskalayan
// kamera ışınına arka plan rengi/şeffaflık uygulanır, çarpanda albedo ve normal alınır.
PixelSample tracePixel(const Scene& scene, const Camera& camera, PathTracer& integrator,
                       const PixelFilterSampler& filter, Sampler& sampler, int x, int y, int sampleIndex,
                       int width, int height, bool wantAovs) {
    sampler.startPixel(x, y);
    sampler.startSample(sampleIndex);

    // Alt-piksel konumu filtrenin dağılımından (filtre önem örneklemesi): örnek pikselin
    // merkezinden filtre yarıçapı kadar uzağa düşebilir, ağırlığı yine 1'dir.
    const Vec2f offset = filter.sample(sampler.get2D());
    Vec2f screen = rasterToScreen(static_cast<float>(x) + 0.5f + offset.x, static_cast<float>(y) + 0.5f + offset.y,
                                  width, height);
    Vec2f lensSample = sampler.get2D();
    Ray ray = camera.generateRay(screen.x, screen.y, lensSample);

    PixelSample out;
    const Background& bg = scene.background();
    if (!wantAovs && bg.mode == Background::Mode::Environment) {
        out.L = integrator.Li(ray, scene, sampler);
        return out;
    }
    PathTracer::PrimaryHit primary;
    out.L = integrator.Li(ray, scene, sampler, &primary);
    if (primary.catcherAlpha >= 0.0f) {
        // Gölge yakalayıcı: kapsama = gölgenin opaklığı; albedo = görünen arka plan.
        out.albedo = clamp01(primary.catcherBackground);
        const Vec3f n = primary.isect.normal;
        out.normal = Color3f(n.x, n.y, n.z);
        out.alpha = bg.mode == Background::Mode::Transparent ? primary.catcherAlpha : 1.0f;
    } else if (primary.hit) {
        out.albedo = clamp01(primaryAlbedo(primary.isect));
        if (primary.coverage >= 0.0f) {
            // İnce camın arkasındaki şeffaf fon: kısmi kapsama.
            out.alpha = primary.coverage;
            const Vec3f n = primary.isect.normal;
            out.normal = Color3f(n.x, n.y, n.z);
            return out;
        }
        const Vec3f n = primary.isect.normal;
        out.normal = Color3f(n.x, n.y, n.z);
        out.alpha = 1.0f;
    } else {
        // OIDN için ıskalayan pikselin albedosu: görünen arka planın rengi (≤ 1).
        const EnvironmentLight* env = scene.environment();
        out.albedo = clamp01(bg.mode == Background::Mode::Color ? bg.color
                             : env ? env->eval(ray.direction) : Color3f::black());
        out.normal = Color3f(0.0f);
        out.alpha = 0.0f;
    }
    return out;
}

void addAovs(AovFilms* aovs, int x, int y, const PixelSample& s) {
    if (!aovs) return;
    aovs->albedo.addSample(x, y, s.albedo);
    aovs->normal.addSampleSigned(x, y, s.normal);
    aovs->alpha.addSample(x, y, Color3f(s.alpha));
}

void denoiseWithAovs(Image& out, const AovFilms* aovs) {
    if (aovs && denoiseAvailable()) {
        Image albedo = aovs->albedo.resolve();
        Image normal = aovs->normal.resolve();
        denoiseImage(out, &albedo, &normal);
    } else {
        denoiseImage(out);
    }
}

} // namespace

// Owen karıştırmalı Sobol (Burley 2020): her piksel ve boyut kendi karıştırmasını
// alır, ilerlemeli render'da her ön-ek (1, 2, 4... örnek) iyi tabakalanır.
std::unique_ptr<Sampler> createRenderSampler(int samplesPerPixel, uint64_t seed) {
    (void)samplesPerPixel;
    return createSobolSampler(seed);
}

Image Renderer::render(const Scene& scene, const Camera& camera, const RenderSettings& settings,
                       const std::atomic<bool>* cancel) {
    Film film(settings.width, settings.height);
    AovFilms aovs;
    const bool wantAovs = settings.denoiseEnabled;
    if (wantAovs) aovs.resize(settings.width, settings.height);

    auto startTime = std::chrono::high_resolution_clock::now();

    std::unique_ptr<Sampler> baseSampler = createRenderSampler(settings.samplesPerPixel, 12345);
    PathTracer integrator(settings.maxBounces, 3, settings.aoStrength, settings.shadowQuality);
    integrator.setClampIndirect(settings.clampIndirect);
    const PixelFilterSampler filter(settings.filter);
    std::unique_ptr<ThreadPool> ownedPool;
    ThreadPool* pool = makePoolOverride(settings, ownedPool);

    const int w = settings.width;
    const int h = settings.height;
    const int spp = std::max(1, settings.samplesPerPixel);

    if (!settings.adaptiveSampling) {
        parallelFor2D(w, h, [&](int xBegin, int xEnd, int yBegin, int yEnd) {
            const uint64_t tileSeed = 12345; // karo boyutundan bağımsız, deterministik
            std::unique_ptr<Sampler> tileSampler = baseSampler->clone(tileSeed);

            for (int y = yBegin; y < yEnd; ++y) {
                for (int x = xBegin; x < xEnd; ++x) {
                    for (int s = 0; s < spp; ++s) {
                        PixelSample ps = tracePixel(scene, camera, integrator, filter, *tileSampler, x, y, s, w, h, wantAovs);
                        film.addSample(x, y, ps.L);
                        if (wantAovs) addAovs(&aovs, x, y, ps);
                    }
                }
            }
        }, settings.tileSize, pool, cancel);
    } else {
        // Uyarlamalı (gürültü eşiği): her piksel en az minAdaptiveSamples alır, sonra her 8
        // örnekte bir göreli standart hatasına bakılır; eşiğin altına inen piksel durur. Bütçe
        // böylece kenarlara, kostiklere ve gölgelere gider; düz fon erken biter.
        const int minSpp = std::clamp(settings.minAdaptiveSamples, 2, spp);
        const float threshold = std::max(1e-4f, settings.noiseThreshold);
        std::atomic<uint64_t> taken{0};
        parallelFor2D(w, h, [&](int xBegin, int xEnd, int yBegin, int yEnd) {
            std::unique_ptr<Sampler> tileSampler = baseSampler->clone(12345);
            uint64_t local = 0;
            for (int y = yBegin; y < yEnd; ++y) {
                for (int x = xBegin; x < xEnd; ++x) {
                    for (int s = 0; s < spp; ++s) {
                        PixelSample ps = tracePixel(scene, camera, integrator, filter, *tileSampler, x, y, s, w, h, wantAovs);
                        film.addSample(x, y, ps.L);
                        if (wantAovs) addAovs(&aovs, x, y, ps);
                        ++local;
                        if (s + 1 >= minSpp && ((s + 1) & 7) == 0 && film.relativeError(x, y) < threshold) break;
                    }
                }
            }
            taken.fetch_add(local, std::memory_order_relaxed);
        }, settings.tileSize, pool, cancel);
        std::cout << "Uyarlamalı: ortalama " << static_cast<double>(taken.load()) / (static_cast<double>(w) * h)
                  << " örnek/piksel (hedef " << spp << ")" << std::endl;
    }

    Image out = film.resolve();
    if (film.rejectedSamples() > 0) {
        std::cerr << "Rejected " << film.rejectedSamples() << " non-finite or negative samples." << std::endl;
    }
    if (settings.denoiseEnabled && !(cancel && cancel->load())) {
        denoiseWithAovs(out, wantAovs ? &aovs : nullptr);
    }

    std::chrono::duration<double> elapsed = std::chrono::high_resolution_clock::now() - startTime;
    std::cout << "Render " << w << "x" << h << " @ " << spp << " spp: " << elapsed.count() << " s" << std::endl;
    return out;
}

Image Renderer::renderProgressive(const Scene& scene, const Camera& camera, const RenderSettings& settings,
                                  const std::function<bool(const Film&, int)>& onPass,
                                  AovFilms* aovsOut) {
    Film film(settings.width, settings.height);
    AovFilms localAovs;
    AovFilms* aovs = aovsOut ? aovsOut : (settings.denoiseEnabled ? &localAovs : nullptr);
    if (aovs) aovs->resize(settings.width, settings.height);

    bool cancelled = false;
    for (int s = 0; s < settings.samplesPerPixel; ++s) {
        renderSamplePass(scene, camera, settings, film, s, nullptr, aovs);
        if (onPass && !onPass(film, s + 1)) {
            cancelled = true;
            break;
        }
    }

    Image out = film.resolve();
    if (settings.denoiseEnabled && !cancelled) denoiseWithAovs(out, aovs);
    return out;
}

bool Renderer::renderSamplePass(const Scene& scene, const Camera& camera, const RenderSettings& settings,
                                Film& accum, int passIndex, const std::atomic<bool>* cancel,
                                AovFilms* aovs) {
    if (accum.width() != settings.width || accum.height() != settings.height) {
        accum.resize(settings.width, settings.height);
    }
    // Çağıranlar RenderSettings'i eşzamanlı değiştirebilir; boyutlar filmden alınır.
    const int w = accum.width();
    const int h = accum.height();
    if (w <= 0 || h <= 0) return true;
    if (aovs && (aovs->albedo.width() != w || aovs->albedo.height() != h)) aovs->resize(w, h);

    std::unique_ptr<Sampler> baseSampler = createRenderSampler(settings.samplesPerPixel, 12345);
    PathTracer integrator(settings.maxBounces, 3, settings.aoStrength, settings.shadowQuality);
    integrator.setClampIndirect(settings.clampIndirect);
    const PixelFilterSampler filter(settings.filter);
    std::unique_ptr<ThreadPool> ownedPool;
    ThreadPool* pool = makePoolOverride(settings, ownedPool);

    // Uyarlamalı örnekleme: kendisi ve 3×3 komşuları gürültü eşiğinin altına inen piksel bu
    // pass'i atlar (komşuluk: temiz bölgenin içinde gürültülü tek piksel kalmasın).
    const bool adaptive = settings.adaptiveSampling && passIndex >= std::max(2, settings.minAdaptiveSamples);
    std::vector<uint8_t> skipMask;
    if (adaptive) {
        const float threshold = std::max(1e-4f, settings.noiseThreshold);
        std::vector<uint8_t> clean(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
        parallelFor2D(w, h, [&](int xBegin, int xEnd, int yBegin, int yEnd) {
            for (int y = yBegin; y < yEnd; ++y)
                for (int x = xBegin; x < xEnd; ++x)
                    clean[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] =
                        accum.relativeError(x, y) < threshold ? 1 : 0;
        }, 64, pool, cancel);
        skipMask.assign(clean.size(), 0);
        parallelFor2D(w, h, [&](int xBegin, int xEnd, int yBegin, int yEnd) {
            for (int y = yBegin; y < yEnd; ++y)
                for (int x = xBegin; x < xEnd; ++x) {
                    bool all = true;
                    for (int dy = -1; dy <= 1 && all; ++dy)
                        for (int dx = -1; dx <= 1 && all; ++dx) {
                            const int nx = std::clamp(x + dx, 0, w - 1), ny = std::clamp(y + dy, 0, h - 1);
                            all = clean[static_cast<size_t>(ny) * static_cast<size_t>(w) + static_cast<size_t>(nx)] != 0;
                        }
                    skipMask[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = all ? 1 : 0;
                }
        }, 64, pool, cancel);
    }

    const bool wantAovs = aovs != nullptr;
    return parallelFor2D(w, h, [&](int xBegin, int xEnd, int yBegin, int yEnd) {
        // Sobol karıştırması pass'ler boyunca SABİT kalmalı; yoksa ardışık örnekler
        // aynı dizinin devamı olmaz ve tabakalanma kaybolur. Piksel ayrışması içeride.
        const uint64_t tileSeed = 12345;
        std::unique_ptr<Sampler> tileSampler = baseSampler->clone(tileSeed);

        for (int y = yBegin; y < yEnd; ++y) {
            // Satır başında iptal kontrolü: büyük karolarda bile tepki ~ms.
            if (cancel && cancel->load(std::memory_order_relaxed)) return;
            for (int x = xBegin; x < xEnd; ++x) {
                if (adaptive && skipMask[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)])
                    continue;
                PixelSample ps = tracePixel(scene, camera, integrator, filter, *tileSampler, x, y, passIndex, w, h, wantAovs);
                accum.addSample(x, y, ps.L);
                if (wantAovs) addAovs(aovs, x, y, ps);
            }
        }
    }, settings.tileSize, pool, cancel);
}

} // namespace photon
