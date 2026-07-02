/// @file transform.cpp
/// @brief Implementation of the Transform class for the PhotonEngine.

#include "core/math/transform.h"

namespace photon {

// ─────────────────────────────────────────────────────────────
// Point transformation
// ─────────────────────────────────────────────────────────────

Vec3f Transform::transformPoint(const Vec3f& p) const {
    // Apply the full 4×4 matrix with w=1 (affine point)
    Vec4f result = m_matrix * Vec4f(p, 1.0f);

    // Perspective divide (should be 1.0 for affine transforms, but
    // included for correctness with projective matrices).
    if (result.w != 1.0f && result.w != 0.0f) {
        float invW = 1.0f / result.w;
        return Vec3f(result.x * invW, result.y * invW, result.z * invW);
    }
    return result.xyz();
}

// ─────────────────────────────────────────────────────────────
// Vector transformation
// ─────────────────────────────────────────────────────────────

Vec3f Transform::transformVector(const Vec3f& v) const {
    // Transform with w=0 (ignores translation)
    return Vec3f(
        m_matrix(0, 0) * v.x + m_matrix(0, 1) * v.y + m_matrix(0, 2) * v.z,
        m_matrix(1, 0) * v.x + m_matrix(1, 1) * v.y + m_matrix(1, 2) * v.z,
        m_matrix(2, 0) * v.x + m_matrix(2, 1) * v.y + m_matrix(2, 2) * v.z
    );
}

// ─────────────────────────────────────────────────────────────
// Normal transformation
// ─────────────────────────────────────────────────────────────

Vec3f Transform::transformNormal(const Vec3f& n) const {
    // Normals transform by the transpose of the inverse matrix.
    // Since m_inverse = M⁻¹, we multiply n by (M⁻¹)ᵀ, which is
    // equivalent to multiplying by the rows of m_inverse.
    return Vec3f(
        m_inverse(0, 0) * n.x + m_inverse(1, 0) * n.y + m_inverse(2, 0) * n.z,
        m_inverse(0, 1) * n.x + m_inverse(1, 1) * n.y + m_inverse(2, 1) * n.z,
        m_inverse(0, 2) * n.x + m_inverse(1, 2) * n.y + m_inverse(2, 2) * n.z
    );
}

// ─────────────────────────────────────────────────────────────
// Ray transformation
// ─────────────────────────────────────────────────────────────

Ray Transform::transformRay(const Ray& r) const {
    Vec3f newOrigin    = transformPoint(r.origin);
    Vec3f newDirection = transformVector(r.direction);
    return Ray(newOrigin, newDirection, r.tMin, r.tMax);
}

// ─────────────────────────────────────────────────────────────
// AABB transformation
// ─────────────────────────────────────────────────────────────

AABB Transform::transformAABB(const AABB& box) const {
    // Transform all 8 corners of the AABB and take the bounding box
    // of the results. Uses Arvo's method (1990) for efficiency:
    // incrementally compute the new min/max from each matrix element.

    Vec3f newMin = Vec3f(m_matrix(0, 3), m_matrix(1, 3), m_matrix(2, 3));
    Vec3f newMax = newMin;

    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            float a = m_matrix(i, j) * box.pMin[j];
            float b = m_matrix(i, j) * box.pMax[j];
            if (a < b) {
                newMin[i] += a;
                newMax[i] += b;
            } else {
                newMin[i] += b;
                newMax[i] += a;
            }
        }
    }

    return AABB(newMin, newMax);
}

// ─────────────────────────────────────────────────────────────
// Composition
// ─────────────────────────────────────────────────────────────

Transform Transform::operator*(const Transform& other) const {
    return Transform(
        m_matrix * other.m_matrix,
        other.m_inverse * m_inverse  // (A·B)⁻¹ = B⁻¹·A⁻¹
    );
}

// ─────────────────────────────────────────────────────────────
// Inverse
// ─────────────────────────────────────────────────────────────

Transform Transform::inverse() const {
    return Transform(m_inverse, m_matrix);
}

// ─────────────────────────────────────────────────────────────
// Static factory methods
// ─────────────────────────────────────────────────────────────

Transform Transform::translate(const Vec3f& delta) {
    Mat4f m = Mat4f::translate(delta);
    Mat4f mInv = Mat4f::translate(-delta);
    return Transform(m, mInv);
}

Transform Transform::rotateX(float angle) {
    Mat4f m    = Mat4f::rotateX(angle);
    Mat4f mInv = Mat4f::rotateX(-angle);
    return Transform(m, mInv);
}

Transform Transform::rotateY(float angle) {
    Mat4f m    = Mat4f::rotateY(angle);
    Mat4f mInv = Mat4f::rotateY(-angle);
    return Transform(m, mInv);
}

Transform Transform::rotateZ(float angle) {
    Mat4f m    = Mat4f::rotateZ(angle);
    Mat4f mInv = Mat4f::rotateZ(-angle);
    return Transform(m, mInv);
}

Transform Transform::scale(const Vec3f& s) {
    Mat4f m = Mat4f::scale(s);
    // Inverse scale: 1/s for each component
    Mat4f mInv = Mat4f::scale(Vec3f(1.0f / s.x, 1.0f / s.y, 1.0f / s.z));
    return Transform(m, mInv);
}

} // namespace photon
