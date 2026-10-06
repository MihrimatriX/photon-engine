// ray.h — Işın: P(t) = origin + t·direction, geçerli aralık [tMin, tMax].
// Kesişim testleri tMax'ı en yakın isabete kısaltarak ilerler; tMin > 0 ise yüzeyden
// çıkan ışının aynı yüzeye tekrar çarpmasını ("shadow acne") önler.
#pragma once

/// @file ray.h
/// @brief Ray representation for ray tracing in the PhotonEngine.
///
/// A ray is defined by an origin point and a direction vector, with
/// parametric bounds [tMin, tMax] constraining the valid interval.

#include "core/math/vec.h"

namespace photon {

/// @brief A parametric ray:  P(t) = origin + t * direction.
///
/// @c tMin is set to a small positive value (1e-4) to avoid self-intersection
/// artifacts (shadow acne). @c tMax defaults to a large value representing
/// an effectively infinite ray.
struct Ray {
    Vec3f origin;                 ///< Ray origin point.
    Vec3f direction;              ///< Ray direction (not necessarily normalized).
    float tMin = 1e-4f;           ///< Minimum parametric distance.
    float tMax = 1e30f;           ///< Maximum parametric distance.

    // ── Constructors ─────────────────────────────────────────

    constexpr Ray() = default;

    constexpr Ray(const Vec3f& origin, const Vec3f& direction)
        : origin(origin), direction(direction) {}

    constexpr Ray(const Vec3f& origin, const Vec3f& direction,
                  float tMin, float tMax)
        : origin(origin), direction(direction), tMin(tMin), tMax(tMax) {}

    // ── Evaluation ───────────────────────────────────────────

    /// Evaluate the ray at parametric distance @p t.
    /// @return origin + t * direction
    constexpr Vec3f at(float t) const {
        return origin + direction * t;
    }
};

} // namespace photon
