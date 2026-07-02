#pragma once

/// @file light.h
/// @brief Abstract Light base class and LightSample structure in PhotonEngine.

#include "core/color/spectrum.h"
#include "core/math/vec.h"
#include "geometry/surface_interaction.h"

namespace photon {

/// @brief Result of sampling a light source from a shading point.
struct LightSample {
    Vec3f wi;          ///< Incident direction from shading point to light source (normalized)
    Color3f Li;        ///< Radiance arriving from the light source at the shading point
    float pdf = 0.0f;  ///< PDF with respect to solid angle
    float distance = 0.0f; ///< Distance from shading point to light source (infinity for directional lights)

    bool isValid() const { return pdf > 0.0f && !Li.isBlack(); }
};

/// @brief Abstract interface for all light sources.
class Light {
public:
    virtual ~Light() = default;

    /// @brief Sample the light source from a shading point.
    ///
    /// @param[in] si Shading point details.
    /// @param[in] sample 2D random sample in [0, 1)².
    /// @return The light sample.
    virtual LightSample sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const = 0;

    /// @brief Return true if this light is a delta distribution (point, directional).
    /// Delta lights cannot be intersected by rays directly, and require special handling in MIS.
    virtual bool isDelta() const = 0;

    /// @brief Approximate total power emitted by the light source.
    virtual Color3f power() const = 0;
};

} // namespace photon
