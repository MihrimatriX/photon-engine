#pragma once

/// @file mat.h
/// @brief 4×4 floating-point matrix for the PhotonEngine renderer.
///
/// Row-major storage: data[row][col]. Provides full arithmetic, inverse
/// via cofactor expansion, and static factory methods for common
/// transformation matrices (translate, rotate, scale, lookAt, perspective).

#include <cmath>
#include <cstring>
#include <cassert>

#include "core/math/constants.h"
#include "core/math/vec.h"

namespace photon {

/// @brief 4×4 column-major-compatible matrix stored as float[4][4] (row-major indexing).
///
/// Convention: data[row][col].
/// Matrix–vector multiplication treats vectors as column vectors:
///   result = M * v
struct Mat4f {
    float data[4][4] = {};

    // ── Constructors ─────────────────────────────────────────

    /// Default constructor — zero matrix.
    constexpr Mat4f() = default;

    /// Construct from explicit row values.
    constexpr Mat4f(float m00, float m01, float m02, float m03,
                    float m10, float m11, float m12, float m13,
                    float m20, float m21, float m22, float m23,
                    float m30, float m31, float m32, float m33) {
        data[0][0] = m00; data[0][1] = m01; data[0][2] = m02; data[0][3] = m03;
        data[1][0] = m10; data[1][1] = m11; data[1][2] = m12; data[1][3] = m13;
        data[2][0] = m20; data[2][1] = m21; data[2][2] = m22; data[2][3] = m23;
        data[3][0] = m30; data[3][1] = m31; data[3][2] = m32; data[3][3] = m33;
    }

    // ── Element access ───────────────────────────────────────

    constexpr float  operator()(int r, int c) const { return data[r][c]; }
    constexpr float& operator()(int r, int c)       { return data[r][c]; }

    // ── Matrix × Matrix ──────────────────────────────────────

    /// Standard 4×4 matrix multiplication.
    constexpr Mat4f operator*(const Mat4f& rhs) const {
        Mat4f result;
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                result.data[r][c] = data[r][0] * rhs.data[0][c]
                                  + data[r][1] * rhs.data[1][c]
                                  + data[r][2] * rhs.data[2][c]
                                  + data[r][3] * rhs.data[3][c];
            }
        }
        return result;
    }

    // ── Matrix × Vec4f ───────────────────────────────────────

    /// Multiply this matrix by a column vector.
    constexpr Vec4f operator*(const Vec4f& v) const {
        return Vec4f(
            data[0][0] * v.x + data[0][1] * v.y + data[0][2] * v.z + data[0][3] * v.w,
            data[1][0] * v.x + data[1][1] * v.y + data[1][2] * v.z + data[1][3] * v.w,
            data[2][0] * v.x + data[2][1] * v.y + data[2][2] * v.z + data[2][3] * v.w,
            data[3][0] * v.x + data[3][1] * v.y + data[3][2] * v.z + data[3][3] * v.w
        );
    }

    // ── Transpose ────────────────────────────────────────────

    /// Return the transpose of this matrix.
    constexpr Mat4f transposed() const {
        Mat4f result;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                result.data[r][c] = data[c][r];
        return result;
    }

    // ── Determinant ──────────────────────────────────────────

    /// Compute the determinant of this 4×4 matrix via cofactor expansion
    /// along the first row.
    constexpr float determinant() const {
        // 2×2 sub-determinants (Laplace expansion)
        float s0 = data[0][0] * data[1][1] - data[1][0] * data[0][1];
        float s1 = data[0][0] * data[1][2] - data[1][0] * data[0][2];
        float s2 = data[0][0] * data[1][3] - data[1][0] * data[0][3];
        float s3 = data[0][1] * data[1][2] - data[1][1] * data[0][2];
        float s4 = data[0][1] * data[1][3] - data[1][1] * data[0][3];
        float s5 = data[0][2] * data[1][3] - data[1][2] * data[0][3];

        float c5 = data[2][2] * data[3][3] - data[3][2] * data[2][3];
        float c4 = data[2][1] * data[3][3] - data[3][1] * data[2][3];
        float c3 = data[2][1] * data[3][2] - data[3][1] * data[2][2];
        float c2 = data[2][0] * data[3][3] - data[3][0] * data[2][3];
        float c1 = data[2][0] * data[3][2] - data[3][0] * data[2][2];
        float c0 = data[2][0] * data[3][1] - data[3][0] * data[2][1];

        return s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
    }

    // ── Inverse ──────────────────────────────────────────────

    /// Compute the inverse of this 4×4 matrix using cofactor expansion.
    ///
    /// Uses the method from "Streaming SIMD Extensions — Inverse of 4×4 Matrix"
    /// (Intel AP-928). Returns identity if the matrix is singular.
    constexpr Mat4f inverse() const {
        float s0 = data[0][0] * data[1][1] - data[1][0] * data[0][1];
        float s1 = data[0][0] * data[1][2] - data[1][0] * data[0][2];
        float s2 = data[0][0] * data[1][3] - data[1][0] * data[0][3];
        float s3 = data[0][1] * data[1][2] - data[1][1] * data[0][2];
        float s4 = data[0][1] * data[1][3] - data[1][1] * data[0][3];
        float s5 = data[0][2] * data[1][3] - data[1][2] * data[0][3];

        float c5 = data[2][2] * data[3][3] - data[3][2] * data[2][3];
        float c4 = data[2][1] * data[3][3] - data[3][1] * data[2][3];
        float c3 = data[2][1] * data[3][2] - data[3][1] * data[2][2];
        float c2 = data[2][0] * data[3][3] - data[3][0] * data[2][3];
        float c1 = data[2][0] * data[3][2] - data[3][0] * data[2][2];
        float c0 = data[2][0] * data[3][1] - data[3][0] * data[2][1];

        float det = s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;

        if (det == 0.0f) return Mat4f::identity();  // Singular matrix fallback

        float invDet = 1.0f / det;

        Mat4f inv;

        inv.data[0][0] = ( data[1][1] * c5 - data[1][2] * c4 + data[1][3] * c3) * invDet;
        inv.data[0][1] = (-data[0][1] * c5 + data[0][2] * c4 - data[0][3] * c3) * invDet;
        inv.data[0][2] = ( data[3][1] * s5 - data[3][2] * s4 + data[3][3] * s3) * invDet;
        inv.data[0][3] = (-data[2][1] * s5 + data[2][2] * s4 - data[2][3] * s3) * invDet;

        inv.data[1][0] = (-data[1][0] * c5 + data[1][2] * c2 - data[1][3] * c1) * invDet;
        inv.data[1][1] = ( data[0][0] * c5 - data[0][2] * c2 + data[0][3] * c1) * invDet;
        inv.data[1][2] = (-data[3][0] * s5 + data[3][2] * s2 - data[3][3] * s1) * invDet;
        inv.data[1][3] = ( data[2][0] * s5 - data[2][2] * s2 + data[2][3] * s1) * invDet;

        inv.data[2][0] = ( data[1][0] * c4 - data[1][1] * c2 + data[1][3] * c0) * invDet;
        inv.data[2][1] = (-data[0][0] * c4 + data[0][1] * c2 - data[0][3] * c0) * invDet;
        inv.data[2][2] = ( data[3][0] * s4 - data[3][1] * s2 + data[3][3] * s0) * invDet;
        inv.data[2][3] = (-data[2][0] * s4 + data[2][1] * s2 - data[2][3] * s0) * invDet;

        inv.data[3][0] = (-data[1][0] * c3 + data[1][1] * c1 - data[1][2] * c0) * invDet;
        inv.data[3][1] = ( data[0][0] * c3 - data[0][1] * c1 + data[0][2] * c0) * invDet;
        inv.data[3][2] = (-data[3][0] * s3 + data[3][1] * s1 - data[3][2] * s0) * invDet;
        inv.data[3][3] = ( data[2][0] * s3 - data[2][1] * s1 + data[2][2] * s0) * invDet;

        return inv;
    }

    // ═════════════════════════════════════════════════════════
    //  Static factory methods
    // ═════════════════════════════════════════════════════════

    /// Identity matrix.
    static constexpr Mat4f identity() {
        return Mat4f(
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        );
    }

    /// Translation matrix.
    static constexpr Mat4f translate(const Vec3f& t) {
        return Mat4f(
            1, 0, 0, t.x,
            0, 1, 0, t.y,
            0, 0, 1, t.z,
            0, 0, 0, 1
        );
    }

    /// Rotation about the X axis by @p angle radians.
    static inline Mat4f rotateX(float angle) {
        float c = std::cos(angle);
        float s = std::sin(angle);
        return Mat4f(
            1, 0,  0, 0,
            0, c, -s, 0,
            0, s,  c, 0,
            0, 0,  0, 1
        );
    }

    /// Rotation about the Y axis by @p angle radians.
    static inline Mat4f rotateY(float angle) {
        float c = std::cos(angle);
        float s = std::sin(angle);
        return Mat4f(
             c, 0, s, 0,
             0, 1, 0, 0,
            -s, 0, c, 0,
             0, 0, 0, 1
        );
    }

    /// Rotation about the Z axis by @p angle radians.
    static inline Mat4f rotateZ(float angle) {
        float c = std::cos(angle);
        float s = std::sin(angle);
        return Mat4f(
            c, -s, 0, 0,
            s,  c, 0, 0,
            0,  0, 1, 0,
            0,  0, 0, 1
        );
    }

    /// Non-uniform scale matrix.
    static constexpr Mat4f scale(const Vec3f& s) {
        return Mat4f(
            s.x, 0,   0,   0,
            0,   s.y, 0,   0,
            0,   0,   s.z, 0,
            0,   0,   0,   1
        );
    }

    /// Right-handed look-at view matrix.
    ///
    /// Constructs a view matrix that transforms world-space coordinates
    /// into camera/eye space where the camera sits at @p eye looking at @p target.
    ///
    /// @param eye    Camera position in world space.
    /// @param target Point the camera is looking at.
    /// @param up     World up direction (typically {0, 1, 0}).
    static inline Mat4f lookAt(const Vec3f& eye, const Vec3f& target, const Vec3f& up) {
        Vec3f f = (target - eye).normalized();  // Forward (-z in view space)
        Vec3f r = f.cross(up).normalized();     // Right   (+x in view space)
        Vec3f u = r.cross(f);                   // True up (+y in view space)

        return Mat4f(
             r.x,  r.y,  r.z, -r.dot(eye),
             u.x,  u.y,  u.z, -u.dot(eye),
            -f.x, -f.y, -f.z,  f.dot(eye),
             0,    0,    0,    1
        );
    }

    /// Symmetric perspective projection matrix (right-handed, depth ∈ [0, 1]).
    ///
    /// @param fovY   Vertical field of view in radians.
    /// @param aspect Aspect ratio (width / height).
    /// @param zNear  Near clipping plane distance (> 0).
    /// @param zFar   Far clipping plane distance (> zNear).
    static inline Mat4f perspective(float fovY, float aspect, float zNear, float zFar) {
        float tanHalfFov = std::tan(fovY * 0.5f);
        float range = zFar - zNear;

        Mat4f result;
        result.data[0][0] = 1.0f / (aspect * tanHalfFov);
        result.data[1][1] = 1.0f / tanHalfFov;
        result.data[2][2] = -(zFar + zNear) / range;
        result.data[2][3] = -(2.0f * zFar * zNear) / range;
        result.data[3][2] = -1.0f;
        result.data[3][3] = 0.0f;

        return result;
    }

    /// Orthographic projection. verticalExtent is the world-space view height.
    static inline Mat4f ortho(float verticalExtent, float aspect, float zNear, float zFar) {
        float top = verticalExtent * 0.5f;
        float bottom = -top;
        float right = top * aspect;
        float left = -right;
        float range = zFar - zNear;

        Mat4f result;
        result.data[0][0] = 2.0f / (right - left);
        result.data[1][1] = 2.0f / (top - bottom);
        result.data[2][2] = -2.0f / range;
        result.data[0][3] = -(right + left) / (right - left);
        result.data[1][3] = -(top + bottom) / (top - bottom);
        result.data[2][3] = -(zFar + zNear) / range;
        result.data[3][3] = 1.0f;
        return result;
    }
};

} // namespace photon
