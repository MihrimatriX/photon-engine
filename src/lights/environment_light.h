#pragma once

/// @file environment_light.h
/// @brief Infinite environment light (HDR background sky) in PhotonEngine.

#include "lights/light.h"
#include "core/image/image.h"

namespace photon {

/// @brief Infinite area light representing an environment map.
class EnvironmentLight : public Light {
public:
    EnvironmentLight(const Image* envMap, float rotation, float intensity)
        : m_envMap(envMap), m_rotation(rotation), m_intensity(intensity) {}

    LightSample sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const override;
    bool isDelta() const override { return false; }
    Color3f power() const override;

    /// Evaluate the background radiance in a given world direction.
    Color3f eval(const Vec3f& direction) const;

private:
    const Image* m_envMap;
    float m_rotation; // in radians
    float m_intensity;
};

} // namespace photon
