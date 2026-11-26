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
#include <cstdint>

namespace photon {

/// Işın görünürlük maskesi: nesneler hangi ışın sınıflarına görünür olacak (Sütun A).
enum VisibilityMask : uint8_t {
    VisCamera       = 1u << 0,
    VisDiffuse      = 1u << 1,
    VisGlossy       = 1u << 2,
    VisTransmission = 1u << 3,
    VisShadow       = 1u << 4,
    VisAll          = 0x1Fu,
};

/// @brief A parametric ray:  P(t) = origin + t * direction.
///
/// @c tMin varsayılanı 0: yüzeyden çıkan ışınlar başlangıçta zaten hata sınırı kadar itilir
/// (geometry/ray_spawn.h). Sabit bir tMin ölçeğe bağlı olurdu (mm'lik CAD'de sivilce, metrelik
/// mücevherde sızma). @c tMax defaults to a large value representing an effectively infinite ray.
struct Ray {
    Vec3f origin;                 ///< Ray origin point.
    Vec3f direction;              ///< Ray direction (not necessarily normalized).
    float tMin = 0.0f;            ///< Minimum parametric distance.
    float tMax = 1e30f;           ///< Maximum parametric distance.
    float time = 0.0f;            ///< Motion blur enstantane zamanı ∈ [0, 1]
    float coneWidth = 0.0f;       ///< Ray Cone başlangıç genişliği (RTG §20)
    float coneSpread = 0.0f;      ///< Ray Cone açısal yayılım hızı (rad/birim)
    uint8_t visibilityMask = VisAll; ///< Bu ışının görebileceği nesne sınıfları

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
