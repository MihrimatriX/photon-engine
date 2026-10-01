#pragma once

/// @file transform.h
/// @brief Affine transformation wrapper around Mat4f for the PhotonEngine.
///
/// Stores both a transformation matrix and its precomputed inverse, enabling
/// efficient transformation of points, vectors, normals, rays, and AABBs.

#include "core/math/vec.h"
#include "core/math/mat.h"
#include "core/math/ray.h"
#include "core/math/aabb.h"

namespace photon {

/// @brief An affine transformation represented by a 4×4 matrix and its inverse.
///
/// Caching the inverse avoids recomputation when transforming normals
/// (which require the transpose of the inverse) or when composing
/// inverse transforms for ray tracing.
class Transform {
public:
    // ── Constructors ─────────────────────────────────────────

    /// Identity transform.
    Transform()
        : m_matrix(Mat4f::identity()), m_inverse(Mat4f::identity()) {}

    /// Construct from a matrix; the inverse is computed automatically.
    explicit Transform(const Mat4f& matrix)
        : m_matrix(matrix), m_inverse(matrix.inverse()) {}

    /// Construct from a matrix and its precomputed inverse.
    Transform(const Mat4f& matrix, const Mat4f& inverse)
        : m_matrix(matrix), m_inverse(inverse) {}

    // ── Accessors ────────────────────────────────────────────

    const Mat4f& matrix()  const { return m_matrix; }
    const Mat4f& inverseMatrix() const { return m_inverse; }

    // ── Transformation methods ───────────────────────────────

    /// Transform a point (applies translation).
    Vec3f transformPoint(const Vec3f& p) const;

    /// Transform a direction vector (ignores translation).
    Vec3f transformVector(const Vec3f& v) const;

    /// Transform a surface normal using the transpose of the inverse matrix.
    /// Returns a non-normalized result — caller should normalize if needed.
    Vec3f transformNormal(const Vec3f& n) const;

    /// Transform a ray (origin as point, direction as vector; tMin/tMax preserved).
    Ray transformRay(const Ray& r) const;

    /// Transform an AABB. The result is still an AABB (axis-aligned), so this
    /// may produce a looser bound than the true transformed box.
    AABB transformAABB(const AABB& box) const;

    // ── Composition ──────────────────────────────────────────

    /// Compose two transforms: result = this × other.
    Transform operator*(const Transform& other) const;

    // ── Inverse ──────────────────────────────────────────────

    /// Return the inverse transform (swaps matrix and inverse).
    Transform inverse() const;

    // ── Static factory methods ───────────────────────────────

    static Transform translate(const Vec3f& delta);
    static Transform rotateX(float angle);
    static Transform rotateY(float angle);
    static Transform rotateZ(float angle);
    static Transform scale(const Vec3f& s);

private:
    Mat4f m_matrix;
    Mat4f m_inverse;
};

inline Transform withTranslation(const Transform& xf, const Vec3f& t) {
    Mat4f m = xf.matrix();
    m(0, 3) = t.x;
    m(1, 3) = t.y;
    m(2, 3) = t.z;
    return Transform(m);
}

} // namespace photon
