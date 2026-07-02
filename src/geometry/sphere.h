#pragma once

/// @file sphere.h
/// @brief 3D Sphere geometry primitive in PhotonEngine.

#include "geometry/shape.h"

namespace photon {

/// @brief Spherical geometry primitive.
class Sphere : public Shape {
public:
    Sphere(const Vec3f& center, float radius, const Material* material)
        : m_center(center), m_radius(radius), m_material(material) {}

    bool intersect(Ray& ray, SurfaceInteraction& isect) const override;
    AABB bounds() const override;

    const Vec3f& center() const { return m_center; }
    float radius() const { return m_radius; }
    const Material* material() const { return m_material; }

private:
    Vec3f m_center;
    float m_radius;
    const Material* m_material;
};

} // namespace photon
