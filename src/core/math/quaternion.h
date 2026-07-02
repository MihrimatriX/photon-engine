#pragma once

/// @file quaternion.h
/// @brief Quaternion class for representing 3D rotations in PhotonEngine.

#include "core/math/vec.h"
#include "core/math/mat.h"
#include <cmath>

namespace photon {

/// @brief Represents a rotation in 3D space.
struct Quaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f; // Identity rotation

    // ── Constructors ─────────────────────────────────────────
    constexpr Quaternion() = default;
    constexpr Quaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

    /// Construct from axis and angle (in radians)
    inline static Quaternion fromAxisAngle(const Vec3f& axis, float angle) {
        float halfAngle = angle * 0.5f;
        float sinHalf = std::sin(halfAngle);
        Vec3f normalizedAxis = axis.normalized();
        return {
            normalizedAxis.x * sinHalf,
            normalizedAxis.y * sinHalf,
            normalizedAxis.z * sinHalf,
            std::cos(halfAngle)
        };
    }

    /// Construct from Euler angles (in radians, pitch/yaw/roll -> Y/X/Z order)
    inline static Quaternion fromEuler(float pitch, float yaw, float roll) {
        float cy = std::cos(yaw * 0.5f);
        float sy = std::sin(yaw * 0.5f);
        float cp = std::cos(pitch * 0.5f);
        float sp = std::sin(pitch * 0.5f);
        float cr = std::cos(roll * 0.5f);
        float sr = std::sin(roll * 0.5f);

        return {
            sr * cp * cy - cr * sp * sy,
            cr * sp * cy + sr * cp * sy,
            cr * cp * sy - sr * sp * cy,
            cr * cp * cy + sr * sp * sy
        };
    }

    // ── Unary Operations ─────────────────────────────────────
    constexpr Quaternion conjugate() const {
        return {-x, -y, -z, w};
    }

    inline float lengthSquared() const {
        return x * x + y * y + z * z + w * w;
    }

    inline float length() const {
        return std::sqrt(lengthSquared());
    }

    inline Quaternion normalized() const {
        float len = length();
        if (len == 0.0f) return {0.0f, 0.0f, 0.0f, 1.0f};
        float invLen = 1.0f / len;
        return {x * invLen, y * invLen, z * invLen, w * invLen};
    }

    inline Quaternion inverse() const {
        float lenSq = lengthSquared();
        if (lenSq == 0.0f) return {0.0f, 0.0f, 0.0f, 1.0f};
        float invLenSq = 1.0f / lenSq;
        return {-x * invLenSq, -y * invLenSq, -z * invLenSq, w * invLenSq};
    }

    // ── Operators ────────────────────────────────────────────
    inline Quaternion operator*(const Quaternion& q) const {
        return {
            w * q.x + x * q.w + y * q.z - z * q.y,
            w * q.y - x * q.z + y * q.w + z * q.x,
            w * q.z + x * q.y - y * q.x + z * q.w,
            w * q.w - x * q.x - y * q.y - z * q.z
        };
    }

    // ── Rotate Vector ────────────────────────────────────────
    inline Vec3f rotateVector(const Vec3f& v) const {
        Vec3f qv(x, y, z);
        Vec3f uv = qv.cross(v);
        Vec3f uuv = qv.cross(uv);
        return v + ((uv * w) + uuv) * 2.0f;
    }

    // ── Conversion to Matrix ─────────────────────────────────
    inline Mat4f toMatrix() const {
        float xx = x * x, yy = y * y, zz = z * z;
        float xy = x * y, xz = x * z, yz = y * z;
        float wx = w * x, wy = w * y, wz = w * z;

        Mat4f mat = Mat4f::identity();
        mat.data[0][0] = 1.0f - 2.0f * (yy + zz);
        mat.data[0][1] = 2.0f * (xy - wz);
        mat.data[0][2] = 2.0f * (xz + wy);

        mat.data[1][0] = 2.0f * (xy + wz);
        mat.data[1][1] = 1.0f - 2.0f * (xx + zz);
        mat.data[1][2] = 2.0f * (yz - wx);

        mat.data[2][0] = 2.0f * (xz - wy);
        mat.data[2][1] = 2.0f * (yz + wx);
        mat.data[2][2] = 1.0f - 2.0f * (xx + yy);

        return mat;
    }

    // ── Spherical Linear Interpolation (SLERP) ──────────────
    inline static Quaternion slerp(const Quaternion& a, const Quaternion& b, float t) {
        Quaternion na = a.normalized();
        Quaternion nb = b.normalized();

        float cosHalfTheta = na.x * nb.x + na.y * nb.y + na.z * nb.z + na.w * na.w;

        // If the dot product is negative, slerp won't take the shorter path.
        // We can fix this by reversing one quaternion.
        if (cosHalfTheta < 0.0f) {
            na.x = -na.x; na.y = -na.y; na.z = -na.z; na.w = -na.w;
            cosHalfTheta = -cosHalfTheta;
        }

        if (std::abs(cosHalfTheta) >= 1.0f - 1e-6f) {
            // Linear interpolation for very close orientations
            return {
                na.x + t * (nb.x - na.x),
                na.y + t * (nb.y - na.y),
                na.z + t * (nb.z - na.z),
                na.w + t * (nb.w - na.w)
            };
        }

        float halfTheta = std::acos(cosHalfTheta);
        float sinHalfTheta = std::sin(halfTheta);

        float ratioA = std::sin((1.0f - t) * halfTheta) / sinHalfTheta;
        float ratioB = std::sin(t * halfTheta) / sinHalfTheta;

        return {
            na.x * ratioA + nb.x * ratioB,
            na.y * ratioA + nb.y * ratioB,
            na.z * ratioA + nb.z * ratioB,
            na.w * ratioA + nb.w * ratioB
        };
    }
};

} // namespace photon
