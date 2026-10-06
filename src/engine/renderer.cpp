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

float luminance(const Color3f& c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
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
                       Sampler& sampler, int x, int y, int sampleIndex,
                       int width, int height, bool wantAovs) {
    sampler.startPixel(x, y);
    sampler.startSample(sampleIndex);

    Vec2f jitter = sampler.get2D();
    Vec2f screen = rasterToScreen(static_cast<float>(x) + jitter.x, static_cast<float>(y) + jitter.y,
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
    if (primary.hit) {
        out.albedo = clamp01(primaryAlbedo(primary.isect));
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
                        PixelSample ps = tracePixel(scene, camera, integrator, *tileSampler, x, y, s, w, h, wantAovs);
                        film.addSample(x, y, ps.L);
                        if (wantAovs) addAovs(&aovs, x, y, ps);
                    }
                }
            }
        }, settings.tileSize, pool, cancel);
    } else {
        // ponytail: önce her yere taban SPP, kalan bütçe yüksek varyanslı piksellere.
        // Tavan: hâlâ O(piksel × spp); yükseltme = karo düzeyinde göreli hata + erken durma.
        const int baseSpp = std::max(4, spp / 4);
        const int extraBudget = spp - baseSpp;

        std::vector<float> sumL(static_cast<size_t>(w) * h, 0.0f);
        std::vector<float> sumL2(static_cast<size_t>(w) * h, 0.0f);

        parallelFor2D(w, h, [&](int xBegin, int xEnd, int yBegin, int yEnd) {
            const uint64_t tileSeed = 12345; // karo boyutundan bağımsız, deterministik
            std::unique_ptr<Sampler> tileSampler = baseSampler->clone(tileSeed);

            for (int y = yBegin; y < yEnd; ++y) {
                for (int x = xBegin; x < xEnd; ++x) {
                    const size_t flat = static_cast<size_t>(y) * w + x;
                    for (int s = 0; s < baseSpp; ++s) {
                        PixelSample ps = tracePixel(scene, camera, integrator, *tileSampler, x, y, s, w, h, wantAovs);
                        film.addSample(x, y, ps.L);
                        if (wantAovs) addAovs(&aovs, x, y, ps);
                        float L = luminance(ps.L);
                        sumL[flat] += L;
                        sumL2[flat] += L * L;
                    }
                }
            }
        }, settings.tileSize, pool, cancel);

        if (extraBudget > 0 && !(cancel && cancel->load())) {
            // Görüntünün ortalama varyansı eşik olur; gürültülü piksellere ek örnek.
            double varSum = 0.0;
            int varCount = 0;
            for (size_t i = 0; i < sumL.size(); ++i) {
                float mean = sumL[i] / static_cast<float>(baseSpp);
                float var = std::max(0.0f, sumL2[i] / static_cast<float>(baseSpp) - mean * mean);
                varSum += var;
                ++varCount;
            }
            const float meanVar = varCount > 0 ? static_cast<float>(varSum / varCount) : 0.0f;
            const float threshold = std::max(1e-6f, meanVar * 0.5f);

            parallelFor2D(w, h, [&](int xBegin, int xEnd, int yBegin, int yEnd) {
                const uint64_t tileSeed = 12345; // aynı dizinin devamı (baseSpp + s)
                std::unique_ptr<Sampler> tileSampler = baseSampler->clone(tileSeed);

                for (int y = yBegin; y < yEnd; ++y) {
                    for (int x = xBegin; x < xEnd; ++x) {
                        const size_t flat = static_cast<size_t>(y) * w + x;
                        float mean = sumL[flat] / static_cast<float>(baseSpp);
                        float var = std::max(0.0f, sumL2[flat] / static_cast<float>(baseSpp) - mean * mean);
                        if (var < threshold) continue;

                        float ratio = std::min(1.0f, var / std::max(meanVar, 1e-6f));
                        int extras = std::max(1, static_cast<int>(std::ceil(ratio * extraBudget)));
                        extras = std::min(extras, extraBudget);

                        for (int s = 0; s < extras; ++s) {
                            PixelSample ps = tracePixel(scene, camera, integrator, *tileSampler,
                                                        x, y, baseSpp + s, w, h, wantAovs);
                            film.addSample(x, y, ps.L);
                            if (wantAovs) addAovs(&aovs, x, y, ps);
                        }
                    }
                }
            }, settings.tileSize, pool, cancel);
        }
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
    std::unique_ptr<ThreadPool> ownedPool;
    ThreadPool* pool = makePoolOverride(settings, ownedPool);

    // Uyarlamalı örnekleme: komşularıyla kontrastı düşük pikseller bu pass'i atlar.
    const bool adaptive = settings.adaptiveSampling && passIndex >= std::max(4, settings.samplesPerPixel / 4);
    std::vector<uint8_t> skipMask;
    if (adaptive) {
        skipMask.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
        parallelFor2D(w, h, [&](int xBegin, int xEnd, int yBegin, int yEnd) {
            for (int y = yBegin; y < yEnd; ++y) {
                for (int x = xBegin; x < xEnd; ++x) {
                    float L = luminance(accum.resolvedPixel(x, y));
                    float neighbor = 0.0f;
                    int n = 0;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            int nx = x + dx, ny = y + dy;
                            if (nx < 0 || ny < 0 || nx >= w || ny >= h || (dx == 0 && dy == 0)) continue;
                            neighbor += luminance(accum.resolvedPixel(nx, ny));
                            ++n;
                        }
                    }
                    float meanN = n > 0 ? neighbor / static_cast<float>(n) : L;
                    if (std::abs(L - meanN) < 0.02f * std::max(1.0f, meanN))
                        skipMask[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = 1;
                }
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
                PixelSample ps = tracePixel(scene, camera, integrator, *tileSampler, x, y, passIndex, w, h, wantAovs);
                accum.addSample(x, y, ps.L);
                if (wantAovs) addAovs(aovs, x, y, ps);
            }
        }
    }, settings.tileSize, pool, cancel);
}

} // namespace photon
