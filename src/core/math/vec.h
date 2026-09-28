#pragma once

/// @file vec.h
/// @brief 2D, 3D, and 4D floating-point vector types for the PhotonEngine.
///
/// Provides Vec2f, Vec3f, Vec4f with full arithmetic, geometric operations,
/// and common factory methods. All operations are constexpr/inline for
/// zero-overhead abstraction.

#include <cmath>
#include <algorithm>
#include <cassert>
#include <ostream>

namespace photon {

// ═════════════════════════════════════════════════════════════
//  Vec2f
// ═════════════════════════════════════════════════════════════

/// @brief 2D floating-point vector.
struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;

    // ── Constructors ─────────────────────────────────────────
    constexpr Vec2f() = default;
    constexpr explicit Vec2f(float v) : x(v), y(v) {}
    constexpr Vec2f(float x, float y) : x(x), y(y) {}

    // ── Element access ───────────────────────────────────────
    constexpr float  operator[](int i) const { return (&x)[i]; }
    constexpr float& operator[](int i)       { return (&x)[i]; }

    // ── Arithmetic (vector ↔ vector) ─────────────────────────
    constexpr Vec2f operator+(const Vec2f& v) const { return {x + v.x, y + v.y}; }
    constexpr Vec2f operator-(const Vec2f& v) const { return {x - v.x, y - v.y}; }
    constexpr Vec2f operator*(const Vec2f& v) const { return {x * v.x, y * v.y}; }
    constexpr Vec2f operator/(const Vec2f& v) const { return {x / v.x, y / v.y}; }

    // ── Arithmetic (vector ↔ scalar) ─────────────────────────
    constexpr Vec2f operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2f operator/(float s) const { float inv = 1.0f / s; return {x * inv, y * inv}; }

    // ── Compound assignment ──────────────────────────────────
    constexpr Vec2f& operator+=(const Vec2f& v) { x += v.x; y += v.y; return *this; }
    constexpr Vec2f& operator-=(const Vec2f& v) { x -= v.x; y -= v.y; return *this; }
    constexpr Vec2f& operator*=(float s)        { x *= s; y *= s; return *this; }
    constexpr Vec2f& operator/=(float s)        { float inv = 1.0f / s; x *= inv; y *= inv; return *this; }

    // ── Unary ────────────────────────────────────────────────
    constexpr Vec2f operator-() const { return {-x, -y}; }

    // ── Comparison ───────────────────────────────────────────
    constexpr bool operator==(const Vec2f& v) const { return x == v.x && y == v.y; }
    constexpr bool operator!=(const Vec2f& v) const { return !(*this == v); }

    // ── Geometric ────────────────────────────────────────────
    constexpr float dot(const Vec2f& v)   const { return x * v.x + y * v.y; }
    constexpr float lengthSquared()       const { return dot(*this); }
    inline    float length()              const { return std::sqrt(lengthSquared()); }
    inline    Vec2f normalized()          const {
        float len = length();
        if (len <= 1e-20f) return Vec2f(0.0f);
        return *this / len;
    }

    // ── Component-wise operations ────────────────────────────
    constexpr Vec2f cwiseMin(const Vec2f& v) const { return {std::min(x, v.x), std::min(y, v.y)}; }
    constexpr Vec2f cwiseMax(const Vec2f& v) const { return {std::max(x, v.x), std::max(y, v.y)}; }
    inline    Vec2f cwiseAbs()               const { return {std::abs(x), std::abs(y)}; }

    // ── Static factories ─────────────────────────────────────
    static constexpr Vec2f zero() { return {0.0f, 0.0f}; }
    static constexpr Vec2f one()  { return {1.0f, 1.0f}; }
};

/// Scalar * Vec2f (left-multiply)
inline constexpr Vec2f operator*(float s, const Vec2f& v) { return v * s; }

/// Stream output for Vec2f
inline std::ostream& operator<<(std::ostream& os, const Vec2f& v) {
    return os << "Vec2f(" << v.x << ", " << v.y << ")";
}

// ═════════════════════════════════════════════════════════════
//  Vec3f
// ═════════════════════════════════════════════════════════════

/// @brief 3D floating-point vector — the workhorse of any renderer.
///
/// Used for positions, directions, colors, normals, and more.
struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    // ── Constructors ─────────────────────────────────────────
    constexpr Vec3f() = default;
    constexpr explicit Vec3f(float v) : x(v), y(v), z(v) {}
    constexpr Vec3f(float x, float y, float z) : x(x), y(y), z(z) {}

    // ── Element access ───────────────────────────────────────
    constexpr float  operator[](int i) const { return (&x)[i]; }
    constexpr float& operator[](int i)       { return (&x)[i]; }

    // ── Arithmetic (vector ↔ vector) ─────────────────────────
    constexpr Vec3f operator+(const Vec3f& v) const { return {x + v.x, y + v.y, z + v.z}; }
    constexpr Vec3f operator-(const Vec3f& v) const { return {x - v.x, y - v.y, z - v.z}; }
    constexpr Vec3f operator*(const Vec3f& v) const { return {x * v.x, y * v.y, z * v.z}; }
    constexpr Vec3f operator/(const Vec3f& v) const { return {x / v.x, y / v.y, z / v.z}; }

    // ── Arithmetic (vector ↔ scalar) ─────────────────────────
    constexpr Vec3f operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3f operator/(float s) const { float inv = 1.0f / s; return {x * inv, y * inv, z * inv}; }

    // ── Compound assignment ──────────────────────────────────
    constexpr Vec3f& operator+=(const Vec3f& v) { x += v.x; y += v.y; z += v.z; return *this; }
    constexpr Vec3f& operator-=(const Vec3f& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
    constexpr Vec3f& operator*=(const Vec3f& v) { x *= v.x; y *= v.y; z *= v.z; return *this; }
    constexpr Vec3f& operator*=(float s)        { x *= s; y *= s; z *= s; return *this; }
    constexpr Vec3f& operator/=(float s)        { float inv = 1.0f / s; x *= inv; y *= inv; z *= inv; return *this; }

    // ── Unary ────────────────────────────────────────────────
    constexpr Vec3f operator-() const { return {-x, -y, -z}; }

    // ── Comparison ───────────────────────────────────────────
    constexpr bool operator==(const Vec3f& v) const { return x == v.x && y == v.y && z == v.z; }
    constexpr bool operator!=(const Vec3f& v) const { return !(*this == v); }

    // ── Geometric ────────────────────────────────────────────

    /// Dot product.
    constexpr float dot(const Vec3f& v) const { return x * v.x + y * v.y + z * v.z; }

    /// Cross product (right-hand rule).
    constexpr Vec3f cross(const Vec3f& v) const {
        return {
            y * v.z - z * v.y,
            z * v.x - x * v.z,
            x * v.y - y * v.x
        };
    }

    constexpr float lengthSquared() const { return dot(*this); }
    inline    float length()        const { return std::sqrt(lengthSquared()); }
    inline    Vec3f normalized()    const {
        float len = length();
        if (len <= 1e-20f) return Vec3f(0.0f);
        return *this / len;
    }

    // ── Component-wise operations ────────────────────────────
    constexpr Vec3f cwiseMin(const Vec3f& v) const { return {std::min(x, v.x), std::min(y, v.y), std::min(z, v.z)}; }
    constexpr Vec3f cwiseMax(const Vec3f& v) const { return {std::max(x, v.x), std::max(y, v.y), std::max(z, v.z)}; }
    inline    Vec3f cwiseAbs()               const { return {std::abs(x), std::abs(y), std::abs(z)}; }

    // ── Static factories ─────────────────────────────────────
    static constexpr Vec3f zero()    { return {0.0f, 0.0f, 0.0f}; }
    static constexpr Vec3f one()     { return {1.0f, 1.0f, 1.0f}; }
    static constexpr Vec3f up()      { return {0.0f, 1.0f, 0.0f}; }
    static constexpr Vec3f right()   { return {1.0f, 0.0f, 0.0f}; }
    static constexpr Vec3f forward() { return {0.0f, 0.0f, -1.0f}; }  // Right-handed, -Z forward
};

/// Scalar * Vec3f (left-multiply)
inline constexpr Vec3f operator*(float s, const Vec3f& v) { return v * s; }

/// Stream output for Vec3f
inline std::ostream& operator<<(std::ostream& os, const Vec3f& v) {
    return os << "Vec3f(" << v.x << ", " << v.y << ", " << v.z << ")";
}

// ── Free-function wrappers (convenience) ─────────────────────

inline constexpr float dot(const Vec3f& a, const Vec3f& b)   { return a.dot(b); }
inline constexpr Vec3f cross(const Vec3f& a, const Vec3f& b) { return a.cross(b); }
inline           Vec3f normalize(const Vec3f& v)              { return v.normalized(); }
inline           float length(const Vec3f& v)                 { return v.length(); }

// ═════════════════════════════════════════════════════════════
//  Vec4f
// ═════════════════════════════════════════════════════════════

/// @brief 4D floating-point vector — used for homogeneous coordinates and SIMD-friendly layouts.
struct Vec4f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    // ── Constructors ─────────────────────────────────────────
    constexpr Vec4f() = default;
    constexpr explicit Vec4f(float v) : x(v), y(v), z(v), w(v) {}
    constexpr Vec4f(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    constexpr Vec4f(const Vec3f& v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}

    // ── Element access ───────────────────────────────────────
    constexpr float  operator[](int i) const { return (&x)[i]; }
    constexpr float& operator[](int i)       { return (&x)[i]; }

    /// Extract the xyz components as a Vec3f.
    constexpr Vec3f xyz() const { return {x, y, z}; }

    // ── Arithmetic (vector ↔ vector) ─────────────────────────
    constexpr Vec4f operator+(const Vec4f& v) const { return {x + v.x, y + v.y, z + v.z, w + v.w}; }
    constexpr Vec4f operator-(const Vec4f& v) const { return {x - v.x, y - v.y, z - v.z, w - v.w}; }
    constexpr Vec4f operator*(const Vec4f& v) const { return {x * v.x, y * v.y, z * v.z, w * v.w}; }
    constexpr Vec4f operator/(const Vec4f& v) const { return {x / v.x, y / v.y, z / v.z, w / v.w}; }

    // ── Arithmetic (vector ↔ scalar) ─────────────────────────
    constexpr Vec4f operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
    constexpr Vec4f operator/(float s) const { float inv = 1.0f / s; return {x * inv, y * inv, z * inv, w * inv}; }

    // ── Compound assignment ──────────────────────────────────
    constexpr Vec4f& operator+=(const Vec4f& v) { x += v.x; y += v.y; z += v.z; w += v.w; return *this; }
    constexpr Vec4f& operator-=(const Vec4f& v) { x -= v.x; y -= v.y; z -= v.z; w -= v.w; return *this; }
    constexpr Vec4f& operator*=(float s)        { x *= s; y *= s; z *= s; w *= s; return *this; }
    constexpr Vec4f& operator/=(float s)        { float inv = 1.0f / s; x *= inv; y *= inv; z *= inv; w *= inv; return *this; }

    // ── Unary ────────────────────────────────────────────────
    constexpr Vec4f operator-() const { return {-x, -y, -z, -w}; }

    // ── Comparison ───────────────────────────────────────────
    constexpr bool operator==(const Vec4f& v) const { return x == v.x && y == v.y && z == v.z && w == v.w; }
    constexpr bool operator!=(const Vec4f& v) const { return !(*this == v); }

    // ── Geometric ────────────────────────────────────────────
    constexpr float dot(const Vec4f& v)   const { return x * v.x + y * v.y + z * v.z + w * v.w; }
    constexpr float lengthSquared()       const { return dot(*this); }
    inline    float length()              const { return std::sqrt(lengthSquared()); }
    inline    Vec4f normalized()          const { float len = length(); assert(len > 0.0f); return *this / len; }

    // ── Component-wise operations ────────────────────────────
    constexpr Vec4f cwiseMin(const Vec4f& v) const { return {std::min(x, v.x), std::min(y, v.y), std::min(z, v.z), std::min(w, v.w)}; }
    constexpr Vec4f cwiseMax(const Vec4f& v) const { return {std::max(x, v.x), std::max(y, v.y), std::max(z, v.z), std::max(w, v.w)}; }
    inline    Vec4f cwiseAbs()               const { return {std::abs(x), std::abs(y), std::abs(z), std::abs(w)}; }

    // ── Static factories ─────────────────────────────────────
    static constexpr Vec4f zero() { return {0.0f, 0.0f, 0.0f, 0.0f}; }
    static constexpr Vec4f one()  { return {1.0f, 1.0f, 1.0f, 1.0f}; }
};

/// Scalar * Vec4f (left-multiply)
inline constexpr Vec4f operator*(float s, const Vec4f& v) { return v * s; }

/// Stream output for Vec4f
inline std::ostream& operator<<(std::ostream& os, const Vec4f& v) {
    return os << "Vec4f(" << v.x << ", " << v.y << ", " << v.z << ", " << v.w << ")";
}

} // namespace photon
