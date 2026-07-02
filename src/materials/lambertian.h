#pragma once

/// @file lambertian.h
/// @brief Lambertian diffuse material in PhotonEngine.

#include "materials/material.h"

namespace photon {

/// @brief Ideal diffuse (Lambertian) reflection material.
class Lambertian : public Material {
public:
    explicit Lambertian(const Color3f& albedo) : m_albedo(albedo) {}

    bool sample(const Vec3f& wo, const SurfaceInteraction& si, const Vec2f& sample,
                Vec3f& wi, Color3f& brdf, float& pdf) const override;

    Color3f eval(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const override;

    float pdf(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const override;

    const Color3f& albedo() const { return m_albedo; }

private:
    Color3f m_albedo;
};

} // namespace photon
