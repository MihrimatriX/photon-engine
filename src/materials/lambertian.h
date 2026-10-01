#pragma once

/// @file lambertian.h
/// @brief Lambertian diffuse material in PhotonEngine.

#include "materials/material.h"

namespace photon {

/// @brief Ideal diffuse (Lambertian) reflection material.
class Lambertian : public Material {
public:
    explicit Lambertian(const Color3f& albedo, const Color3f& emission = Color3f::black())
        : m_albedo(albedo), m_emission(emission) {}

    bool sample(const Vec3f& wo, const SurfaceInteraction& si, const Vec2f& sample,
                Vec3f& wi, Color3f& brdf, float& pdf) const override;

    Color3f eval(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const override;

    float pdf(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const override;

    Color3f emitted(const SurfaceInteraction&) const override { return m_emission; }

    const Color3f& albedo() const { return m_albedo; }

private:
    Color3f m_albedo;
    Color3f m_emission;
};

} // namespace photon
