#pragma once

/// @file frame.h
/// @brief Orthonormal coordinate frame wrapper around normal vectors for the PhotonEngine.

#include "core/math/vec.h"
#include <cmath>

namespace photon {

/// @brief Orthonormal coordinate frame (local coordinate system).
///
/// Used to transform rays/vectors between world space and local shading space.
struct Frame {
    Vec3f s; // Tangent
    Vec3f t; // Bitangent
    Vec3f n; // Normal (always Z-up locally)

    // ── Constructors ─────────────────────────────────────────
    constexpr Frame() : s(1, 0, 0), t(0, 1, 0), n(0, 0, 1) {}
    constexpr Frame(const Vec3f& s, const Vec3f& t, const Vec3f& n) : s(s), t(t), n(n) {}

    /// Construct from normal vector. Builds orthonormal basis using Duff et al. 2017 method.
    inline explicit Frame(const Vec3f& normal) : n(normal) {
        // Duff et al. 2017 method: "Building an Orthonormal Basis, Revisited"
        float sign = std::copysign(1.0f, n.z);
        const float a = -1.0f / (sign + n.z);
        const float b = n.x * n.y * a;
        s = Vec3f(1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x);
        t = Vec3f(b, sign + n.y * n.y * a, -n.y);
    }

    // ── Transformations ──────────────────────────────────────

    /// Transform world vector to local shading space
    constexpr Vec3f toLocal(const Vec3f& v) const {
        return {v.dot(s), v.dot(t), v.dot(n)};
    }

    /// Transform local vector to world space
    constexpr Vec3f toWorld(const Vec3f& v) const {
        return s * v.x + t * v.y + n * v.z;
    }

    // ── Shading coordinate utilities (local vectors) ─────────
    static constexpr float cosTheta(const Vec3f& w) { return w.z; }
    static constexpr float cosTheta2(const Vec3f& w) { return w.z * w.z; }
    static inline    float sinTheta2(const Vec3f& w) { return std::max(0.0f, 1.0f - cosTheta2(w)); }
    static inline    float sinTheta(const Vec3f& w) { return std::sqrt(sinTheta2(w)); }
    static inline    float tanTheta(const Vec3f& w) { return sinTheta(w) / cosTheta(w); }
    static inline    float tanTheta2(const Vec3f& w) { return sinTheta2(w) / cosTheta2(w); }

    static inline float cosPhi(const Vec3f& w) {
        float sinT = sinTheta(w);
        return (sinT == 0.0f) ? 1.0f : clamp(w.x / sinT, -1.0f, 1.0f);
    }

    static inline float sinPhi(const Vec3f& w) {
        float sinT = sinTheta(w);
        return (sinT == 0.0f) ? 0.0f : clamp(w.y / sinT, -1.0f, 1.0f);
    }

private:
    static constexpr float clamp(float val, float lo, float hi) {
        return (val < lo) ? lo : ((val > hi) ? hi : val);
    }
};

} // namespace photon
