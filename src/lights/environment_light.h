#pragma once

/// @file environment_light.h
/// @brief Infinite environment light (HDR background sky) in PhotonEngine.

#include "lights/light.h"
#include "core/image/image.h"
#include "core/math/constants.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace photon {

/// Equirect UV for a world direction. phi = atan2(-z, x) (CPU / path-tracer convention).
inline Vec2f directionToEquirect(const Vec3f& direction, float rotation = 0.0f) {
    Vec3f d = direction.normalized();
    float theta = std::acos(std::clamp(d.y, -1.0f, 1.0f));
    float phi = std::atan2(-d.z, d.x) + PI + rotation;
    phi = std::fmod(phi, TWO_PI);
    if (phi < 0.0f) phi += TWO_PI;
    return Vec2f(phi / TWO_PI, theta / PI);
}

/// @brief Infinite area light representing an environment map.
class EnvironmentLight : public Light {
public:
    EnvironmentLight(const Image* envMap, float rotation, float intensity);

    LightSample sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const override;
    bool isDelta() const override { return false; }
    Color3f power() const override;

    /// Evaluate the background radiance in a given world direction.
    Color3f eval(const Vec3f& direction) const;

    /// Solid-angle PDF of sampleLi. Both overloads use the luminance CDF (the normal is unused).
    float pdfLi(const Vec3f& wi) const;
    float pdfLi(const Vec3f& wi, const Vec3f& normal) const;

    /// Last marginal CDF value. ~1 when the map has energy, 0 when it does not.
    float distributionIntegral() const {
        return m_marginalCdf.empty() ? 0.0f : m_marginalCdf.back();
    }

private:
    void buildDistribution();
    float solidAnglePdf(const Vec3f& wi) const;

    const Image* m_envMap;
    float m_rotation; // in radians
    float m_intensity;

    int m_width = 0;
    int m_height = 0;
    float m_funcInt = 0.0f;
    std::vector<float> m_func;          // luminance * sin(theta) per pixel
    std::vector<float> m_marginalCdf;   // height + 1, prefix, back == 1
    std::vector<float> m_condCdf;       // per row, width + 1
};

} // namespace photon
