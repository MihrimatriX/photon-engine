#include "engine/renderer.h"
#include "core/threading/parallel.h"
#include "samplers/independent_sampler.h"
#include "samplers/stratified_sampler.h"
#include "integrators/path_tracer.h"
#include <iostream>
#include <chrono>

namespace photon {

Image Renderer::render(const Scene& scene, const Camera& camera, const RenderSettings& settings) {
    Image img(settings.width, settings.height);

    auto startTime = std::chrono::high_resolution_clock::now();
    std::cout << "Starting render: " << settings.width << "x" << settings.height 
              << " @ " << settings.samplesPerPixel << " SPP (Tiles: " 
              << settings.tileSize << "x" << settings.tileSize << ")" << std::endl;

    // Use a base sampler (IndependentSampler is simple and default)
    std::unique_ptr<Sampler> baseSampler = std::make_unique<IndependentSampler>(12345);
    PathTracer integrator(settings.maxBounces);

    // Render using multi-threaded tiles
    parallelFor2D(settings.width, settings.height, [&](int xBegin, int xEnd, int yBegin, int yEnd) {
        // Clone sampler for this specific tile to prevent thread conflicts
        uint64_t tileSeed = 12345 ^ (static_cast<uint64_t>(xBegin) * 73821 + static_cast<uint64_t>(yBegin) * 1923);
        std::unique_ptr<Sampler> tileSampler = baseSampler->clone(tileSeed);

        for (int y = yBegin; y < yEnd; ++y) {
            for (int x = xBegin; x < xEnd; ++x) {
                // Initialize sampler for this pixel
                tileSampler->startPixel(x, y);

                for (int s = 0; s < settings.samplesPerPixel; ++s) {
                    tileSampler->startSample(s);

                    // Jitter sub-pixel coordinates for anti-aliasing
                    Vec2f jitter = tileSampler->get2D();
                    float u = (static_cast<float>(x) + jitter.x) / static_cast<float>(settings.width);
                    float v = (static_cast<float>(y) + jitter.y) / static_cast<float>(settings.height);

                    // Sample lens for Depth of Field
                    Vec2f lensSample = tileSampler->get2D();

                    // Generate ray and estimate radiance
                    Ray ray = camera.generateRay(u, v, lensSample);
                    Color3f Li = integrator.Li(ray, scene, *tileSampler);

                    // Add sample to image buffer
                    img.addSample(x, y, Li);
                }
            }
        }
    }, settings.tileSize);

    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;
    std::cout << "Render finished in " << elapsed.count() << " seconds." << std::endl;

    return img;
}

void Renderer::renderProgressive(const Scene& scene, const Camera& camera, const RenderSettings& settings,
                                 std::function<void(const Image&, int)> callback) {
    Image img(settings.width, settings.height);

    std::unique_ptr<Sampler> baseSampler = std::make_unique<IndependentSampler>(12345);
    PathTracer integrator(settings.maxBounces);

    // For progressive rendering, we loop over each sample pass sequentially (spp),
    // and run parallelFor2D for each sample pass, then invoke the callback.
    for (int s = 0; s < settings.samplesPerPixel; ++s) {
        parallelFor2D(settings.width, settings.height, [&](int xBegin, int xEnd, int yBegin, int yEnd) {
            // Re-seed based on tile coordinates AND current pass index
            uint64_t tileSeed = 12345 ^ (static_cast<uint64_t>(xBegin) * 73821 + static_cast<uint64_t>(yBegin) * 1923 + s * 991231);
            std::unique_ptr<Sampler> tileSampler = baseSampler->clone(tileSeed);

            for (int y = yBegin; y < yEnd; ++y) {
                for (int x = xBegin; x < xEnd; ++x) {
                    tileSampler->startPixel(x, y);
                    tileSampler->startSample(s);

                    Vec2f jitter = tileSampler->get2D();
                    float u = (static_cast<float>(x) + jitter.x) / static_cast<float>(settings.width);
                    float v = (static_cast<float>(y) + jitter.y) / static_cast<float>(settings.height);

                    Vec2f lensSample = tileSampler->get2D();

                    Ray ray = camera.generateRay(u, v, lensSample);
                    Color3f Li = integrator.Li(ray, scene, *tileSampler);

                    img.addSample(x, y, Li);
                }
            }
        }, settings.tileSize);

        // Call the callback after each sample pass (s + 1 is the current SPP count)
        if (callback) {
            callback(img, s + 1);
        }
    }
}

} // namespace photon
