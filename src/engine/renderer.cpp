#include "engine/renderer.h"
#include "engine/denoiser.h"
#include "core/threading/parallel.h"
#include "samplers/stratified_sampler.h"
#include "integrators/path_tracer.h"
#include "materials/disney.h"
#include "materials/dielectric.h"
#include "materials/lambertian.h"
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

Color3f samplePixel(const Scene& scene, const Camera& camera, PathTracer& integrator,
                    Sampler& sampler, int x, int y, int sampleIndex,
                    int width, int height) {
    sampler.startPixel(x, y);
    sampler.startSample(sampleIndex);

    Vec2f jitter = sampler.get2D();
    float u = (static_cast<float>(x) + jitter.x) / static_cast<float>(width);
    float v = (static_cast<float>(y) + jitter.y) / static_cast<float>(height);
    Vec2f lensSample = sampler.get2D();
    Ray ray = camera.generateRay(u, v, lensSample);
    return integrator.Li(ray, scene, sampler);
}

float luminance(const Color3f& c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

void samplerGrid(int spp, int& xs, int& ys) {
    spp = std::max(1, spp);
    int root = std::max(1, static_cast<int>(std::lround(std::sqrt(static_cast<double>(spp)))));
    xs = 1;
    for (int i = root; i >= 1; --i) {
        if (spp % i == 0) {
            xs = i;
            break;
        }
    }
    ys = spp / xs;
}

Color3f primaryAlbedo(const SurfaceInteraction& si) {
    if (!si.material) return Color3f::black();
    if (const auto* disney = dynamic_cast<const DisneyMaterial*>(si.material))
        return disney->resolve(si).baseColor;
    if (const auto* lambert = dynamic_cast<const Lambertian*>(si.material))
        return lambert->albedo();
    if (const auto* glass = dynamic_cast<const Dielectric*>(si.material))
        return glass->tint();
    return Color3f(0.5f);
}

void fillPrimaryAovs(const Scene& scene, const Camera& camera, int w, int h,
                     Image& albedo, Image& normal) {
    albedo.resize(w, h);
    normal.resize(w, h);
    parallelFor2D(w, h, [&](int xBegin, int xEnd, int yBegin, int yEnd) {
        for (int y = yBegin; y < yEnd; ++y) {
            for (int x = xBegin; x < xEnd; ++x) {
                float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(w);
                float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
                Ray ray = camera.generateRay(u, v, Vec2f(0.5f, 0.5f));
                SurfaceInteraction isect;
                if (scene.intersect(ray, isect)) {
                    albedo.addSample(x, y, primaryAlbedo(isect));
                    Vec3f n = isect.normal;
                    normal.addSample(x, y, Color3f(n.x, n.y, n.z));
                } else {
                    albedo.addSample(x, y, Color3f::black());
                    normal.addSample(x, y, Color3f(0.0f, 0.0f, 1.0f));
                }
            }
        }
    }, 32, nullptr);
}

} // namespace

std::unique_ptr<Sampler> createRenderSampler(int samplesPerPixel, uint64_t seed) {
    int xs = 1, ys = 1;
    samplerGrid(std::max(1, samplesPerPixel), xs, ys);
    return std::make_unique<StratifiedSampler>(xs, ys, seed);
}

Image Renderer::render(const Scene& scene, const Camera& camera, const RenderSettings& settings) {
    Image img(settings.width, settings.height);

    auto startTime = std::chrono::high_resolution_clock::now();
    std::cout << "Starting render: " << settings.width << "x" << settings.height
              << " @ " << settings.samplesPerPixel << " SPP (Tiles: "
              << settings.tileSize << "x" << settings.tileSize << ")" << std::endl;

    std::unique_ptr<Sampler> baseSampler = createRenderSampler(settings.samplesPerPixel, 12345);
    PathTracer integrator(settings.maxBounces, 3, settings.aoStrength, settings.shadowQuality);
    std::unique_ptr<ThreadPool> ownedPool;
    ThreadPool* pool = makePoolOverride(settings, ownedPool);

    const int w = settings.width;
    const int h = settings.height;
    const int spp = std::max(1, settings.samplesPerPixel);

    if (!settings.adaptiveSampling) {
        parallelFor2D(w, h, [&](int xBegin, int xEnd, int yBegin, int yEnd) {
            uint64_t tileSeed = 12345 ^ (static_cast<uint64_t>(xBegin) * 73821 + static_cast<uint64_t>(yBegin) * 1923);
            std::unique_ptr<Sampler> tileSampler = baseSampler->clone(tileSeed);

            for (int y = yBegin; y < yEnd; ++y) {
                for (int x = xBegin; x < xEnd; ++x) {
                    for (int s = 0; s < spp; ++s) {
                        img.addSample(x, y, samplePixel(scene, camera, integrator, *tileSampler, x, y, s, w, h));
                    }
                }
            }
        }, settings.tileSize, pool);
    } else {
        // ponytail: base SPP everywhere, then spend remaining budget on high-variance pixels.
        // Ceiling: O(pixels * spp) still; upgrade = tile-level variance + early stop.
        const int baseSpp = std::max(4, spp / 4);
        const int extraBudget = spp - baseSpp;

        std::vector<float> sumL(static_cast<size_t>(w) * h, 0.0f);
        std::vector<float> sumL2(static_cast<size_t>(w) * h, 0.0f);

        parallelFor2D(w, h, [&](int xBegin, int xEnd, int yBegin, int yEnd) {
            uint64_t tileSeed = 12345 ^ (static_cast<uint64_t>(xBegin) * 73821 + static_cast<uint64_t>(yBegin) * 1923);
            std::unique_ptr<Sampler> tileSampler = baseSampler->clone(tileSeed);

            for (int y = yBegin; y < yEnd; ++y) {
                for (int x = xBegin; x < xEnd; ++x) {
                    const size_t flat = static_cast<size_t>(y) * w + x;
                    for (int s = 0; s < baseSpp; ++s) {
                        Color3f Li = samplePixel(scene, camera, integrator, *tileSampler, x, y, s, w, h);
                        img.addSample(x, y, Li);
                        float L = luminance(Li);
                        sumL[flat] += L;
                        sumL2[flat] += L * L;
                    }
                }
            }
        }, settings.tileSize, pool);

        if (extraBudget > 0) {
            // Mean variance across image → threshold; noisy pixels get up to extraBudget more samples.
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
                uint64_t tileSeed = 99991 ^ (static_cast<uint64_t>(xBegin) * 73821 + static_cast<uint64_t>(yBegin) * 1923);
                std::unique_ptr<Sampler> tileSampler = baseSampler->clone(tileSeed);

                for (int y = yBegin; y < yEnd; ++y) {
                    for (int x = xBegin; x < xEnd; ++x) {
                        const size_t flat = static_cast<size_t>(y) * w + x;
                        float mean = sumL[flat] / static_cast<float>(baseSpp);
                        float var = std::max(0.0f, sumL2[flat] / static_cast<float>(baseSpp) - mean * mean);
                        if (var < threshold) continue;

                        // Scale extras by how noisy vs mean (cap at extraBudget).
                        float ratio = std::min(1.0f, var / std::max(meanVar, 1e-6f));
                        int extras = std::max(1, static_cast<int>(std::ceil(ratio * extraBudget)));
                        extras = std::min(extras, extraBudget);

                        for (int s = 0; s < extras; ++s) {
                            Color3f Li = samplePixel(scene, camera, integrator, *tileSampler,
                                                    x, y, baseSpp + s, w, h);
                            img.addSample(x, y, Li);
                        }
                    }
                }
            }, settings.tileSize, pool);
        }
    }

    if (settings.denoiseEnabled) {
        if (denoiseAvailable()) {
            Image albedo, normal;
            fillPrimaryAovs(scene, camera, w, h, albedo, normal);
            denoiseImage(img, &albedo, &normal);
        } else {
            denoiseImage(img);
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;
    std::cout << "Render finished in " << elapsed.count() << " seconds." << std::endl;

    return img;
}

void Renderer::renderProgressive(const Scene& scene, const Camera& camera, const RenderSettings& settings,
                                 std::function<void(const Image&, int)> callback) {
    Image img(settings.width, settings.height);

    for (int s = 0; s < settings.samplesPerPixel; ++s) {
        renderSamplePass(scene, camera, settings, img, s);
        if (callback) {
            callback(img, s + 1);
        }
    }

    if (settings.denoiseEnabled) {
        if (denoiseAvailable()) {
            Image albedo, normal;
            fillPrimaryAovs(scene, camera, settings.width, settings.height, albedo, normal);
            denoiseImage(img, &albedo, &normal);
        } else {
            denoiseImage(img);
        }
        if (callback) {
            callback(img, settings.samplesPerPixel);
        }
    }
}

void Renderer::renderSamplePass(const Scene& scene, const Camera& camera, const RenderSettings& settings,
                                Image& accum, int passIndex) {
    if (accum.width() != settings.width || accum.height() != settings.height) {
        accum.resize(settings.width, settings.height);
    }

    std::unique_ptr<Sampler> baseSampler = createRenderSampler(settings.samplesPerPixel, 12345);
    PathTracer integrator(settings.maxBounces, 3, settings.aoStrength, settings.shadowQuality);
    std::unique_ptr<ThreadPool> ownedPool;
    ThreadPool* pool = makePoolOverride(settings, ownedPool);

    // Snapshot dims — callers may mutate RenderSettings concurrently.
    const int w = accum.width();
    const int h = accum.height();
    if (w <= 0 || h <= 0) return;

    const bool adaptive = settings.adaptiveSampling && passIndex >= std::max(4, settings.samplesPerPixel / 4);
    std::vector<uint8_t> skipMask;
    if (adaptive) {
        skipMask.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                Color3f c = accum.getAveragedPixel(x, y);
                float L = luminance(c);
                float neighbor = 0.0f;
                int n = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h || (dx == 0 && dy == 0)) continue;
                        neighbor += luminance(accum.getAveragedPixel(nx, ny));
                        ++n;
                    }
                }
                float meanN = n > 0 ? neighbor / n : L;
                float contrast = std::abs(L - meanN);
                if (contrast < 0.02f * std::max(1.0f, meanN)) {
                    skipMask[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = 1;
                }
            }
        }
    }

    parallelFor2D(w, h, [&](int xBegin, int xEnd, int yBegin, int yEnd) {
        uint64_t tileSeed = 12345 ^ (static_cast<uint64_t>(xBegin) * 73821 +
                                     static_cast<uint64_t>(yBegin) * 1923 +
                                     static_cast<uint64_t>(passIndex) * 991231);
        std::unique_ptr<Sampler> tileSampler = baseSampler->clone(tileSeed);

        for (int y = yBegin; y < yEnd; ++y) {
            for (int x = xBegin; x < xEnd; ++x) {
                if (adaptive && skipMask[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)])
                    continue;
                accum.addSample(x, y, samplePixel(scene, camera, integrator, *tileSampler, x, y, passIndex, w, h));
            }
        }
    }, settings.tileSize, pool);
}

} // namespace photon
