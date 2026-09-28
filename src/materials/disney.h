#pragma once

/// @file disney.h
/// @brief Simplified Disney Principled BRDF in PhotonEngine.

#include "materials/material.h"
#include "core/image/image.h"
#include <algorithm>
#include <memory>
#include <string>

namespace photon {

/// @brief Simplified Disney Principled BRDF.
///
/// Combines a Burley diffuse lobe and a Cook-Torrance GGX specular microfacet lobe,
/// parameterized by metallic, roughness, and specular strength. Optional texture maps
/// (albedo / normal / roughness / metalness) are sampled at SurfaceInteraction::uv.
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

    const std::string& albedoMap() const { return m_albedoMap; }
    const std::string& normalMap() const { return m_normalMap; }
    const std::string& roughnessMap() const { return m_roughnessMap; }
    const std::string& metalnessMap() const { return m_metalnessMap; }
    void setAlbedoMap(const std::string& p);
    void setNormalMap(const std::string& p);
    void setRoughnessMap(const std::string& p);
    void setMetalnessMap(const std::string& p);

    /// Resolved scalar/texture parameters at a shading point (for tests / debug).
    struct ShadingParams {
        Color3f baseColor;
        float metallic = 0.0f;
        float roughness = 0.5f;
        float specular = 0.5f;
        float clearCoat = 0.0f;
        float clearCoatRoughness = 0.03f;
        Vec3f normal{0, 1, 0};
    };
    ShadingParams resolve(const SurfaceInteraction& si) const;

private:
    static std::shared_ptr<Image> loadMap(const std::string& path);

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
    std::shared_ptr<Image> m_albedoTex;
    std::shared_ptr<Image> m_normalTex;
    std::shared_ptr<Image> m_roughnessTex;
    std::shared_ptr<Image> m_metalnessTex;
};

} // namespace photon
