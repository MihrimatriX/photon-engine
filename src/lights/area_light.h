#pragma once

/// @file area_light.h
/// @brief Rectangular area light source in PhotonEngine.

#include "lights/light.h"

namespace photon {

/// @brief Rectangular area light emitting diffuse radiance from one side.
class AreaLight : public Light {
public:
    AreaLight(const Vec3f& position, const Vec3f& u, const Vec3f& v, const Color3f& radiance)
        : m_position(position), m_u(u), m_v(v), m_radiance(radiance) {
        Vec3f normal = u.cross(v);
        m_area = normal.length();
        m_normal = normal.normalized();
    }

    LightSample sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const override;
    bool isDelta() const override { return false; }
    Color3f power() const override;

    const Vec3f& position() const { return m_position; }
    const Vec3f& u() const { return m_u; }
    const Vec3f& v() const { return m_v; }
    const Vec3f& normal() const { return m_normal; }
    float area() const { return m_area; }
    const Color3f& radiance() const { return m_radiance; }
    void setRadiance(const Color3f& r) { m_radiance = r; }

private:
    Vec3f m_position;
    Vec3f m_u;
    Vec3f m_v;
    Color3f m_radiance;
    
    Vec3f m_normal;
    float m_area;
};

} // namespace photon
