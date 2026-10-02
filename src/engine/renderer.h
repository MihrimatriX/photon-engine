#pragma once

/// @file renderer.h
/// @brief Multi-threaded tile-based renderer in PhotonEngine.

#include "engine/scene.h"
#include "engine/render_settings.h"
#include "camera/camera.h"
#include "core/image/film.h"
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

    /// @brief Synchronously render the scene, all samples of a tile at a time.
    /// @return The resolved (averaged) HDR image, denoised when settings ask for it.
    Image render(const Scene& scene, const Camera& camera, const RenderSettings& settings);

    /// @brief Render one sample per pixel per pass until samplesPerPixel is reached.
    ///
    /// @param onPass Called after every pass with the film and the samples per
    ///               pixel so far. Return false to stop early (cancel).
    /// @return The resolved image of the last completed pass, denoised when
    ///         settings ask for it and the render was not cancelled.
    Image renderProgressive(const Scene& scene, const Camera& camera, const RenderSettings& settings,
                            const std::function<bool(const Film&, int)>& onPass);

    /// @brief Render a single sample pass into an existing film.
    void renderSamplePass(const Scene& scene, const Camera& camera, const RenderSettings& settings,
                          Film& accum, int passIndex);
};

} // namespace photon
