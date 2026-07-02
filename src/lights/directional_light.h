#pragma once

/// @file directional_light.h
/// @brief Distant directional light source (like the sun) in PhotonEngine.

#include "lights/light.h"

namespace photon {

/// @brief Directional light source at infinite distance.
class DirectionalLight : public Light {
public:
    /// @param direction Direction in which light is travelling (normalized)
    /// @param irradiance Irradiance arriving on a surface perpendicular to the light
    DirectionalLight(const Vec3f& direction, const Color3f& irradiance)
        : m_direction(direction.normalized()), m_irradiance(irradiance) {}

    LightSample sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const override;
    bool isDelta() const override { return true; }
    Color3f power() const override;

private:
    Vec3f m_direction; // Direction light travels TO
    Color3f m_irradiance;
};

} // namespace photon
