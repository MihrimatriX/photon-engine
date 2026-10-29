// light_bounds.cpp — Işık sınırlarının önem tahmini ve birleşimi (PBRT-v4 LightBounds,
// DirectionCone::Union). Açılar "kısıtlı çıkarma" ile hesaplanır: θ' = max(0, θw − θo − θb),
// yani noktaya giden yönün, koninin ve sınır kutusunun gördüğü açının en iyimser farkı.
#include "lights/light_bounds.h"
#include "core/math/constants.h"

#include <algorithm>
#include <cmath>

namespace photon {

namespace {

float safeSqrt(float x) { return std::sqrt(std::max(0.0f, x)); }
float safeAcos(float x) { return std::acos(std::clamp(x, -1.0f, 1.0f)); }

/// cos(max(0, a − b)) — a ve b açılarının sinüs/kosinüsünden.
float cosSubClamped(float sinA, float cosA, float sinB, float cosB) {
    if (cosA > cosB) return 1.0f;
    return cosA * cosB + sinA * sinB;
}

/// sin(max(0, a − b)).
float sinSubClamped(float sinA, float cosA, float sinB, float cosB) {
    if (cosA > cosB) return 0.0f;
    return sinA * cosB - cosA * sinB;
}

/// @p v'yi birim @p axis etrafında @p theta kadar döndür (Rodrigues).
Vec3f rotateAround(const Vec3f& v, const Vec3f& axis, float theta) {
    const float c = std::cos(theta), s = std::sin(theta);
    return v * c + axis.cross(v) * s + axis * (axis.dot(v) * (1.0f - c));
}

} // namespace

float LightBounds::importance(const Vec3f& p, const Vec3f& n) const {
    if (!(phi > 0.0f)) return 0.0f;
    const Vec3f pc = bounds.centroid();
    const Vec3f diag = bounds.diagonal();
    // Nokta kutunun içindeyse uzaklık sıfıra inmesin: kutu yarı köşegeninin karesiyle sınırla.
    const float d2 = std::max((p - pc).lengthSquared(), 0.25f * diag.lengthSquared());
    if (!(d2 > 0.0f)) return phi;

    // Işıktan noktaya yön ve koni ekseniyle açısı θw.
    Vec3f wi = p - pc;
    const float len = wi.length();
    wi = len > 0.0f ? wi / len : w;
    float cosW = w.dot(wi);
    if (twoSided) cosW = std::abs(cosW);
    const float sinW = safeSqrt(1.0f - cosW * cosW);

    // Kutunun noktadan görülen yarı açısı θb (sınırlayıcı küre ile).
    const float r2 = 0.25f * diag.lengthSquared();
    const float dc2 = (p - pc).lengthSquared();
    const float cosB = dc2 <= r2 ? -1.0f : safeSqrt(1.0f - r2 / dc2);
    const float sinB = safeSqrt(1.0f - cosB * cosB);

    // θ' = max(0, θw − θo − θb): yayım konisinin noktaya en yakın kenarı.
    const float sinO = safeSqrt(1.0f - cosThetaO * cosThetaO);
    const float cosX = cosSubClamped(sinW, cosW, sinO, cosThetaO);
    const float sinX = sinSubClamped(sinW, cosW, sinO, cosThetaO);
    const float cosP = cosSubClamped(sinX, cosX, sinB, cosB);
    // Yayılma konisinin dışında: hiçbir ışık noktaya ulaşamaz (tek yüzlü ışığın arkası).
    if (cosP < cosThetaE) return 0.0f;

    float imp = phi * cosP / d2;
    if (n.lengthSquared() > 0.0f) {
        // Alıcı kosinüsü, yine en iyimser açıyla. |·|: geçirgen yüzeyler arkadan da alır.
        const float cosI = std::abs(wi.dot(n));
        const float sinI = safeSqrt(1.0f - cosI * cosI);
        imp *= cosSubClamped(sinI, cosI, sinB, cosB);
    }
    return std::max(imp, 0.0f);
}

LightBounds unionBounds(const LightBounds& a, const LightBounds& b) {
    if (!(a.phi > 0.0f)) return b;
    if (!(b.phi > 0.0f)) return a;
    LightBounds u;
    u.bounds = a.bounds.merged(b.bounds);
    u.phi = a.phi + b.phi;
    u.cosThetaE = std::min(a.cosThetaE, b.cosThetaE);
    u.twoSided = a.twoSided || b.twoSided;

    // Koni birleşimi: biri diğerini içeriyorsa o; yoksa ikisini kapsayan en dar koni.
    const float thetaA = safeAcos(a.cosThetaO), thetaB = safeAcos(b.cosThetaO);
    const float thetaD = safeAcos(a.w.dot(b.w));
    if (std::min(thetaD + thetaB, PI) <= thetaA) {
        u.w = a.w;
        u.cosThetaO = a.cosThetaO;
        return u;
    }
    if (std::min(thetaD + thetaA, PI) <= thetaB) {
        u.w = b.w;
        u.cosThetaO = b.cosThetaO;
        return u;
    }
    const float thetaO = 0.5f * (thetaA + thetaD + thetaB);
    const Vec3f axis = a.w.cross(b.w);
    if (thetaO >= PI || axis.lengthSquared() < 1e-12f) {
        u.w = a.w;
        u.cosThetaO = -1.0f; // tüm küre
        return u;
    }
    u.w = rotateAround(a.w, axis.normalized(), thetaO - thetaA).normalized();
    u.cosThetaO = std::cos(thetaO);
    return u;
}

} // namespace photon
