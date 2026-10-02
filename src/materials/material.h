#pragma once

/// @file material.h
/// @brief Abstract Material base class in PhotonEngine.

#include "core/color/spectrum.h"
#include "core/math/vec.h"
#include "geometry/surface_interaction.h"

namespace photon {

/// @brief Abstract class representing a surface material/BSDF.
///
/// Uses standard PBRT conventions: all vectors wo and wi are normalized
/// and point AWAY from the surface interaction point in the local coordinate frame.
class Material {
public:
    virtual ~Material() = default;

    /// @brief Sample an incident direction wi given an outgoing direction wo.
    ///
    /// @param[in]  wo       Outgoing direction (points toward viewer, away from surface).
    /// @param[in]  si       Surface interaction details.
    /// @param[in]  sample   2D random sample in [0, 1)².
    /// @param[out] wi       Sampled incident direction (points toward light/next bounce, away from surface).
    /// @param[out] brdf     Evaluated BRDF/BSDF value for (wo, wi).
    /// @param[out] pdf      PDF value of sampling wi.
    /// @return true if sampling succeeded, false if invalid.
    virtual bool sample(const Vec3f& wo, const SurfaceInteraction& si, const Vec2f& sample,
                        Vec3f& wi, Color3f& brdf, float& pdf) const = 0;

    /// @brief Evaluate the BRDF/BSDF for a given pair of directions (wo, wi).
    virtual Color3f eval(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const = 0;

    /// @brief Evaluate the probability density function for sampling wi.
    virtual float pdf(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const = 0;

    /// @brief Returns emitted radiance from the surface (for light sources).
    virtual Color3f emitted(const SurfaceInteraction&) const {
        return Color3f::black();
    }
};

} // namespace photon
