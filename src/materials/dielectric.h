#pragma once

/// @file dielectric.h
/// @brief Dielectric (glass/transparent) material in PhotonEngine.

#include "materials/material.h"

namespace photon {

/// @brief Glass-like dielectric refraction and reflection material.
/// Roughness 0 is a delta surface. Roughness > 0 is a GGX microfacet BTDF.
class Dielectric : public Material {
public:
    Dielectric(float ior, const Color3f& tint, float roughness = 0.0f)
        : m_ior(ior), m_tint(tint), m_roughness(roughness) {}

    bool sample(const Vec3f& wo, const SurfaceInteraction& si, const Vec2f& sample,
                Vec3f& wi, Color3f& brdf, float& pdf) const override;

    Color3f eval(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const override;

    float pdf(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const override;

    float ior() const { return m_ior; }
    const Color3f& tint() const { return m_tint; }
    float roughness() const { return m_roughness; }

private:
    float m_ior;
    Color3f m_tint;
    float m_roughness = 0.0f;
};

} // namespace photon
