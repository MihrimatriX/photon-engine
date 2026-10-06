// mirror.h — Mükemmel ayna: gelen ışık tek bir yöne (yansıma yönüne) gider.
// BRDF bir Dirac deltasıdır: isDelta() true, eval/pdf 0 döner, ışık yalnız sample() ile izlenir.
#pragma once

/// @file mirror.h
/// @brief Mirror (perfect specular reflection) material in PhotonEngine.

#include "materials/material.h"

namespace photon {

/// @brief Ideal mirror reflection material.
class Mirror : public Material {
public:
    explicit Mirror(const Color3f& reflectance) : m_reflectance(reflectance) {}

    bool isDelta() const override { return true; }
    bool sample(const Vec3f& wo, const SurfaceInteraction& si, const Vec2f& sample,
                Vec3f& wi, Color3f& brdf, float& pdf) const override;

    Color3f eval(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const override;

    float pdf(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const override;

    const Color3f& reflectance() const { return m_reflectance; }

private:
    Color3f m_reflectance;
};

} // namespace photon
