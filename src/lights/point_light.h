// point_light.h — Nokta ışık: tek bir noktadan her yöne eşit ışıyan delta ışık.
// İdeal bir kavramdır (sonsuz küçük kaynak): keskin gölge verir, kameraya görünmez.
#pragma once

/// @file point_light.h
/// @brief Omnidirectional point light source in PhotonEngine.

#include "lights/light.h"

namespace photon {

/// @brief Point light source emitting light equally in all directions.
class PointLight : public Light {
public:
    PointLight(const Vec3f& position, const Color3f& intensity)
        : m_position(position), m_intensity(intensity) {}

    LightSample sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const override;
    bool isDelta() const override { return true; }
    Color3f power() const override;

private:
    Vec3f m_position;
    Color3f m_intensity;
};

} // namespace photon
