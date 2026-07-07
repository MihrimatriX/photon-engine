#pragma once

/// @file disney.h
/// @brief Simplified Disney Principled BRDF in PhotonEngine.

#include "materials/material.h"
#include <algorithm>
#include <string>

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
    float clearCoat() const { return m_clearCoat; }
    float clearCoatRoughness() const { return m_clearCoatRoughness; }

    void setBaseColor(const Color3f& c) { m_baseColor = c; }
    void setMetallic(float v) { m_metallic = v; }
    void setRoughness(float v) { m_roughness = std::max(0.001f, v); }
    void setSpecular(float v) { m_specular = v; }
    void setClearCoat(float v) { m_clearCoat = std::clamp(v, 0.0f, 1.0f); }
    void setClearCoatRoughness(float v) { m_clearCoatRoughness = std::max(0.001f, v); }

    // ponytail: texture paths stored for UI; CPU integrator ignores until wired
    const std::string& albedoMap() const { return m_albedoMap; }
    const std::string& normalMap() const { return m_normalMap; }
    const std::string& roughnessMap() const { return m_roughnessMap; }
    const std::string& metalnessMap() const { return m_metalnessMap; }
    void setAlbedoMap(const std::string& p) { m_albedoMap = p; }
    void setNormalMap(const std::string& p) { m_normalMap = p; }
    void setRoughnessMap(const std::string& p) { m_roughnessMap = p; }
    void setMetalnessMap(const std::string& p) { m_metalnessMap = p; }

private:
    Color3f m_baseColor;
    float m_metallic;
    float m_roughness;
    float m_specular;
    float m_clearCoat = 0.0f;
    float m_clearCoatRoughness = 0.03f;
    std::string m_albedoMap;
    std::string m_normalMap;
    std::string m_roughnessMap;
    std::string m_metalnessMap;
};

} // namespace photon
