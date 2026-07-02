#pragma once

/// @file disney.h
/// @brief Simplified Disney Principled BRDF in PhotonEngine.

#include "materials/material.h"

namespace photon {

/// @brief Simplified Disney Principled BRDF.
///
/// Combines a Burley diffuse lobe and a Cook-Torrance GGX specular microfacet lobe,
/// parameterized by metallic, roughness, and specular strength.
class DisneyMaterial : public Material {
public:
    DisneyMaterial(const Color3f& baseColor, float metallic, float roughness, float specular)
        : m_baseColor(baseColor), m_metallic(metallic), m_roughness(std::max(0.001f, roughness)), m_specular(specular) {}

    bool sample(const Vec3f& wo, const SurfaceInteraction& si, const Vec2f& sample,
                Vec3f& wi, Color3f& brdf, float& pdf) const override;

    Color3f eval(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const override;

    float pdf(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const override;

    const Color3f& baseColor() const { return m_baseColor; }
    float metallic() const { return m_metallic; }
    float roughness() const { return m_roughness; }
    float specular() const { return m_specular; }

private:
    Color3f m_baseColor;
    float m_metallic;
    float m_roughness;
    float m_specular;
};

} // namespace photon
