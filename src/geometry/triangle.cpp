#include "geometry/triangle.h"
#include <cmath>

namespace photon {

bool Triangle::intersect(Ray& ray, SurfaceInteraction& isect) const {
    Vec3f edge1 = m_v1 - m_v0;
    Vec3f edge2 = m_v2 - m_v0;
    Vec3f h = ray.direction.cross(edge2);
    float a = edge1.dot(h);

    if (a > -1e-6f && a < 1e-6f) {
        return false; // Ray is parallel to the triangle
    }

    float f = 1.0f / a;
    Vec3f s = ray.origin - m_v0;
    float u = f * s.dot(h);

    if (u < 0.0f || u > 1.0f) {
        return false;
    }

    Vec3f q = s.cross(edge1);
    float v = f * ray.direction.dot(q);

    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }

    float t = f * edge2.dot(q);

    if (t < ray.tMin || t > ray.tMax) {
        return false;
    }

    // Hit confirmed!
    ray.tMax = t; // Update ray's tMax

    isect.t = t;
    isect.point = ray.at(t);

    // Compute barycentric weights
    float w = 1.0f - u - v;

    // Geometric normal (cross product of edges)
    Vec3f geomNormal = edge1.cross(edge2).normalized();

    // Shading normal (interpolate if available)
    Vec3f shadingNormal;
    if (m_n0.lengthSquared() > 0.0f && m_n1.lengthSquared() > 0.0f && m_n2.lengthSquared() > 0.0f) {
        shadingNormal = (m_n0 * w + m_n1 * u + m_n2 * v).normalized();
    } else {
        shadingNormal = geomNormal;
    }

    isect.setFaceNormal(ray, shadingNormal);

    // Interpolate UV coordinates
    isect.uv = m_uv0 * w + m_uv1 * u + m_uv2 * v;

    // Calculate tangent (direction of increasing U)
    // dP = dU * T + dV * B
    // We solve for T and B
    Vec2f deltaUV1 = m_uv1 - m_uv0;
    Vec2f deltaUV2 = m_uv2 - m_uv0;

    float r = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
    if (std::abs(r) < 1e-6f) {
        // Fallback tangent
        float sign = std::copysign(1.0f, isect.normal.z);
        float a_coeff = -1.0f / (sign + isect.normal.z);
        float b_coeff = isect.normal.x * isect.normal.y * a_coeff;
        isect.tangent = Vec3f(1.0f + sign * isect.normal.x * isect.normal.x * a_coeff, sign * b_coeff, -sign * isect.normal.x);
    } else {
        float invR = 1.0f / r;
        isect.tangent = (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * invR;
    }
    isect.tangent = isect.tangent.normalized();

    isect.material = m_material;

    return true;
}

AABB Triangle::bounds() const {
    AABB box;
    box.pMin = m_v0.cwiseMin(m_v1).cwiseMin(m_v2);
    box.pMax = m_v0.cwiseMax(m_v1).cwiseMax(m_v2);
    return box;
}

} // namespace photon
