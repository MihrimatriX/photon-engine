#pragma once

/// @file renderer.h
/// @brief Multi-threaded tile-based renderer in PhotonEngine.

#include "engine/scene.h"
#include "engine/render_settings.h"
#include "camera/camera.h"
#include "core/image/image.h"
#include <cstdint>
#include <functional>
#include <memory>

namespace photon {

class Sampler;

/// Production sampler: stratified grid over samplesPerPixel. Tests keep IndependentSampler.
std::unique_ptr<Sampler> createRenderSampler(int samplesPerPixel, uint64_t seed = 12345);

/// @brief Orchestrates the rendering process using a tile-based approach on a ThreadPool.
class Renderer {
public:
    Renderer() = default;

    /// @brief Synchronously render the scene.
    ///
    /// @param scene The 3D scene.
    /// @param camera The camera viewing the scene.
    /// @param settings Config settings.
    /// @return Rendered HDR image.
    Image render(const Scene& scene, const Camera& camera, const RenderSettings& settings);

    /// @brief Progressively render the scene, calling a callback function periodically.
    ///
    /// @param scene The 3D scene.
    /// @param camera The camera.
    /// @param settings Config settings.
    /// @param callback Callback invoked after each sample pass per pixel, receives (image, currentSPP).
    void renderProgressive(const Scene& scene, const Camera& camera, const RenderSettings& settings,
                           std::function<void(const Image&, int)> callback);

    /// @brief Render a single sample pass into an existing accumulation buffer.
    void renderSamplePass(const Scene& scene, const Camera& camera, const RenderSettings& settings,
                          Image& accum, int passIndex);
};

} // namespace photon
