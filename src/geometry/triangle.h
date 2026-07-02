#pragma once

/// @file triangle.h
/// @brief Triangle shape primitive in PhotonEngine.

#include "geometry/shape.h"

namespace photon {

/// @brief Triangle shape primitive.
class Triangle : public Shape {
public:
    Triangle(const Vec3f& v0, const Vec3f& v1, const Vec3f& v2,
             const Vec3f& n0, const Vec3f& n1, const Vec3f& n2,
             const Vec2f& uv0, const Vec2f& uv1, const Vec2f& uv2,
             const Material* material)
        : m_v0(v0), m_v1(v1), m_v2(v2),
          m_n0(n0), m_n1(n1), m_n2(n2),
          m_uv0(uv0), m_uv1(uv1), m_uv2(uv2),
          m_material(material) {}

    bool intersect(Ray& ray, SurfaceInteraction& isect) const override;
    AABB bounds() const override;

    const Vec3f& v0() const { return m_v0; }
    const Vec3f& v1() const { return m_v1; }
    const Vec3f& v2() const { return m_v2; }

private:
    Vec3f m_v0, m_v1, m_v2;
    Vec3f m_n0, m_n1, m_n2;
    Vec2f m_uv0, m_uv1, m_uv2;
    const Material* m_material;
};

} // namespace photon
