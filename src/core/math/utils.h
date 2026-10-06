// utils.h — Küçük matematik yardımcıları: clamp/lerp, ikinci derece denklem, MIS güç sezgiseli,
// yansıma/kırılma vektörleri ve Fresnel denklemleri. Renderer'ın her yerinden kullanılır.
#pragma once

/// @file utils.h
/// @brief Inline math utility functions for the PhotonEngine renderer.
///
/// Provides common operations used across the renderer: clamping, interpolation,
/// quadratic solving, importance-sampling heuristics, coordinate conversions,
/// reflection/refraction, and Fresnel equations.

#include <cmath>
#include <algorithm>
#include <utility>

#include "core/math/constants.h"
#include "core/math/vec.h"

namespace photon {

// ─────────────────────────────────────────────────────────────
// Scalar utilities
// ─────────────────────────────────────────────────────────────

/// Clamp @p val to the range [@p lo, @p hi].
template <typename T>
inline constexpr T clamp(T val, T lo, T hi) {
    return (val < lo) ? lo : ((val > hi) ? hi : val);
}

/// Linear interpolation between @p a and @p b by factor @p t ∈ [0, 1].
inline constexpr float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

/// Remap @p value from the range [@p fromLow, @p fromHigh] to [@p toLow, @p toHigh].
/// Performs an unclamped linear mapping.
inline constexpr float remap(float value, float fromLow, float fromHigh,
                              float toLow, float toHigh) {
    return toLow + (value - fromLow) / (fromHigh - fromLow) * (toHigh - toLow);
}

/// Convert degrees to radians.
inline constexpr float deg2rad(float degrees) {
    return degrees * DEG_TO_RAD;
}

/// Convert radians to degrees.
inline constexpr float rad2deg(float radians) {
    return radians * RAD_TO_DEG;
}

// ─────────────────────────────────────────────────────────────
// Quadratic solver
// ─────────────────────────────────────────────────────────────

/// Solve the quadratic equation  a·t² + b·t + c = 0.
///
/// @param[in]  a, b, c  Coefficients of the quadratic.
/// @param[out] t0, t1   The two roots (t0 ≤ t1) if they exist.
/// @return @c true if real roots exist (discriminant ≥ 0).
///
/// Uses the numerically stable formulation to avoid catastrophic cancellation.
inline bool solveQuadratic(float a, float b, float c, float& t0, float& t1) {
    // Degenerate case: linear equation
    if (std::abs(a) < EPSILON) {
        if (std::abs(b) < EPSILON) return false;
        t0 = t1 = -c / b;
        return true;
    }

    float discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0.0f) return false;

    float sqrtDisc = std::sqrt(discriminant);

    // Numerically stable formulation (Press et al.)
    // Klasik (−b ± √Δ)/2a formülünde b ≈ √Δ olduğunda çıkarma "felaket iptali" ile
    // basamak kaybeder. Çözüm: işaretleri aynı olan toplamı kullan, q = −½(b + sgn(b)√Δ);
    // kökler q/a ve c/q (Vieta: t0·t1 = c/a). Hiçbir adımda yakın sayılar çıkarılmaz.
    float q = -0.5f * (b + (b < 0.0f ? -sqrtDisc : sqrtDisc));

    t0 = q / a;
    t1 = c / q;

    if (t0 > t1) std::swap(t0, t1);
    return true;
}

// ─────────────────────────────────────────────────────────────
// Importance sampling
// ─────────────────────────────────────────────────────────────

/// Power heuristic for Multiple Importance Sampling (Veach, 1995).
///
/// Computes the weight for strategy @e f using β = 2:
///   w_f = (nf · pf)² / ((nf · pf)² + (ng · pg)²)
///
/// @param nf   Number of samples from distribution f
/// @param fPdf PDF value of distribution f
/// @param ng   Number of samples from distribution g
/// @param gPdf PDF value of distribution g
/// @return MIS weight for distribution f
// MIS (Çoklu Önem Örneklemesi): aynı ışık katkısı hem BSDF örneklemesiyle hem ışık
// örneklemesiyle bulunabilir. Ağırlıkların toplamı 1 olduğu sürece tahmin yansızdır;
// güç sezgiseli (β=2) hangi strateji o yönde daha "emin"se (pdf büyükse) ona daha çok
// ağırlık verir ve gürültüyü dengeli sezgiselden daha iyi bastırır (Veach tezi, bölüm 9).
inline float powerHeuristic(int nf, float fPdf, int ng, float gPdf) {
    float f = static_cast<float>(nf) * fPdf;
    float g = static_cast<float>(ng) * gPdf;
    float f2 = f * f;
    return f2 / (f2 + g * g);
}

// ─────────────────────────────────────────────────────────────
// Coordinate conversions
// ─────────────────────────────────────────────────────────────

/// Convert spherical coordinates (theta, phi) to a unit Cartesian direction.
///
/// Convention:
///  - theta: polar angle from +Y axis
///  - phi:   azimuthal angle from +X axis in the XZ plane
inline Vec3f sphericalToCartesian(float theta, float phi) {
    float sinTheta = std::sin(theta);
    return Vec3f(
        sinTheta * std::cos(phi),
        std::cos(theta),
        sinTheta * std::sin(phi)
    );
}

// ─────────────────────────────────────────────────────────────
// Reflection & refraction
// ─────────────────────────────────────────────────────────────

/// Reflect direction @p wo about normal @p n.
///
/// @param wo Outgoing direction (pointing away from surface).
/// @param n  Surface normal (unit length).
/// @return Reflected direction.
// wo'nun normal bileşeni (wo·n)n korunur, teğet bileşeni ters çevrilir:
// r = (wo·n)n − (wo − (wo·n)n) = 2(wo·n)n − wo.
inline Vec3f reflectVec(const Vec3f& wo, const Vec3f& n) {
    return 2.0f * dot(wo, n) * n - wo;
}

/// Refract direction @p wi through a surface with normal @p n and
/// relative index of refraction @p eta = ηi/ηt (PBRT-v3 `Refract` convention).
///
/// @param[in]  wi  Incident direction, pointing AWAY from the surface (same side as @p n).
/// @param[in]  n   Unit surface normal on the same side as @p wi (dot(wi, n) > 0).
/// @param[in]  eta Ratio of indices of refraction ηi / ηt (ηi = medium @p wi is in).
/// @param[out] wt  Refracted direction, pointing away from the surface on the other side.
/// @return @c true if refraction occurs, @c false for total internal reflection.
///
/// Matematik (Snell, vektör biçimi): wi'nin teğet bileşeni wi - cosθi·n'dir.
/// Kırılan ışının teğet bileşeni Snell gereği eta kat büyür ve YÖNÜ TERS olur
/// (ışın yüzeyi geçer): -eta·(wi - cosθi·n). Normal bileşeni -cosθt·n'dir.
/// Toplam: wt = -eta·wi + (eta·cosθi - cosθt)·n. Dikkat: wi yüzeye DOĞRU değil,
/// yüzeyden DIŞARI bakar. Eski belge "toward" diyordu; Dielectric de buna uyup
/// -wo verince her kırılma teğet düzlemde aynalanıyordu (bulgu core-1).
inline bool refractVec(const Vec3f& wi, const Vec3f& n, float eta, Vec3f& wt) {
    float cosThetaI = dot(n, wi);
    float sin2ThetaI = std::max(0.0f, 1.0f - cosThetaI * cosThetaI);
    float sin2ThetaT = eta * eta * sin2ThetaI;

    // Total internal reflection
    if (sin2ThetaT >= 1.0f) return false;

    float cosThetaT = std::sqrt(1.0f - sin2ThetaT);
    wt = eta * (-wi) + (eta * cosThetaI - cosThetaT) * n;
    return true;
}

// ─────────────────────────────────────────────────────────────
// Fresnel equations
// ─────────────────────────────────────────────────────────────

/// Schlick's approximation of the Fresnel reflectance.
///
/// @param cosTheta Cosine of the angle between the incident direction and normal.
/// @param F0       Reflectance at normal incidence.
/// @return Approximate Fresnel reflectance.
// F(θ) ≈ F0 + (1 − F0)(1 − cosθ)⁵ (Schlick 1994). Dik gelişte F0, sıyırma açısında 1'e
// gider. F0 = ((η−1)/(η+1))²; cam (η=1.5) için ≈ 0.04. Üs 5 için pow yerine çarpım zinciri.
inline float fresnelSchlick(float cosTheta, float F0) {
    float oneMinusCos = 1.0f - cosTheta;
    float oneMinusCos2 = oneMinusCos * oneMinusCos;
    float oneMinusCos5 = oneMinusCos2 * oneMinusCos2 * oneMinusCos;
    return F0 + (1.0f - F0) * oneMinusCos5;
}

/// Exact Fresnel reflectance for a dielectric interface.
///
/// Handles both external (etaI < etaT) and internal (etaI > etaT) incidence,
/// including total internal reflection.
///
/// @param cosThetaI Cosine of the incident angle (clamped internally).
/// @param etaI      Index of refraction of the incident medium.
/// @param etaT      Index of refraction of the transmitted medium.
/// @return Fresnel reflectance ∈ [0, 1].
// Maxwell denklemlerinden gelen tam Fresnel: rs (dik/s) ve rp (paralel/p) kutuplanmış
// genlik yansıtma katsayılarıdır. Kutuplanmamış ışık için güç yansıtması bunların
// karelerinin ortalamasıdır: F = (rs² + rp²)/2. Geçen kısım 1 − F (PBRT 4. baskı, 9.3).
inline float fresnelDielectric(float cosThetaI, float etaI, float etaT) {
    cosThetaI = clamp(cosThetaI, -1.0f, 1.0f);

    // Potentially swap indices so that cosThetaI refers to the outside
    bool entering = cosThetaI > 0.0f;
    if (!entering) {
        std::swap(etaI, etaT);
        cosThetaI = std::abs(cosThetaI);
    }

    // Snell's law: compute sin²θt
    float sinThetaI = std::sqrt(std::max(0.0f, 1.0f - cosThetaI * cosThetaI));
    float sinThetaT = (etaI / etaT) * sinThetaI;

    // Total internal reflection
    if (sinThetaT >= 1.0f) return 1.0f;

    float cosThetaT = std::sqrt(std::max(0.0f, 1.0f - sinThetaT * sinThetaT));

    // Fresnel equations for s- and p-polarized light
    float rs = (etaT * cosThetaI - etaI * cosThetaT) /
               (etaT * cosThetaI + etaI * cosThetaT);
    float rp = (etaI * cosThetaI - etaT * cosThetaT) /
               (etaI * cosThetaI + etaT * cosThetaT);

    return 0.5f * (rs * rs + rp * rp);
}

// ─────────────────────────────────────────────────────────────
// Photometric
// ─────────────────────────────────────────────────────────────

/// Compute the perceptual luminance of an RGB color.
///
/// Uses ITU-R BT.709 coefficients (same as sRGB primaries).
/// @return Scalar luminance value.
// Katsayılar, doğrusal sRGB/BT.709 ana renklerinden CIE Y'ye dönüşüm matrisinin orta
// satırıdır; göz yeşile en duyarlı olduğundan G ağırlığı en büyüktür. Girdi DOĞRUSAL olmalı.
inline constexpr float luminance(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

} // namespace photon
