#pragma once

/// @file integrator.h
/// @brief Abstract Integrator base class in PhotonEngine.

#include "core/color/spectrum.h"
#include "core/math/ray.h"

namespace photon {

class Scene;   // Forward declaration
class Sampler; // Forward declaration

/// @brief Abstract base class for rendering integrators (e.g. PathTracer).
class Integrator {
public:
    virtual ~Integrator() = default;

    /// @brief Estimate the incident radiance along a ray.
    ///
    /// @param[in] ray     The ray along which to estimate radiance.
    /// @param[in] scene   The 3D scene geometry and light sources.
    /// @param[in] sampler Random number sampler.
    /// @return Estimated radiance.
    virtual Color3f Li(const Ray& ray, const Scene& scene, Sampler& sampler) const = 0;
};

} // namespace photon
