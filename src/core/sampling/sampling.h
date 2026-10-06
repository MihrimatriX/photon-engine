// sampling.h — Monte Carlo örnekleme dönüşümleri ("warp"): [0,1)² düzgün sayıları küre,
// yarıküre, disk ve üçgen üzerine eşler; her birinin pdf'i de yanında verilir.
// Yöntem: ters CDF (inversion) — hedef dağılımın kümülatifini tersine çevirmek
// (PBRT 4. baskı, bölüm A.5 ve 2.3). Tahminci f(x)/pdf(x) olduğundan pdf doğru olmalı.
#pragma once

/// @file sampling.h
/// @brief Sampling utilities for Monte Carlo rendering in PhotonEngine.

#include "core/math/vec.h"
#include "core/math/constants.h"
#include "core/random/rng.h"
#include <cmath>
#include <algorithm>

namespace photon {

// ─── Sampling Functions ──────────────────────────────────────────────────────

/// Uniformly sample a direction on the unit sphere.
// Arşimet'in şapka kutusu teoremi: küre üzerinde z'ye göre alan düzgündür
// (her z dilimi aynı alana sahip). Bu yüzden z = 1 − 2u₁ düzgün, φ = 2πu₂ düzgün seçilir;
// r = √(1 − z²). Toplam alan 4π → pdf (katı açı başına) = 1/(4π).
inline Vec3f uniformSampleSphere(Vec2f u) {
    float z   = 1.0f - 2.0f * u.x;
    float r   = std::sqrt(std::max(0.0f, 1.0f - z * z));
    float phi = TWO_PI * u.y;
    return {r * std::cos(phi), r * std::sin(phi), z};
}

/// PDF for uniform sphere sampling: 1 / (4π).
inline float uniformSpherePdf() {
    return 0.25f * INV_PI;
}

/// Uniformly sample a direction on the upper hemisphere (z >= 0).
// Küre ile aynı fikir, sadece z = cosθ ∈ [0, 1): alan 2π → pdf = 1/(2π).
inline Vec3f uniformSampleHemisphere(Vec2f u) {
    float z   = u.x;
    float r   = std::sqrt(std::max(0.0f, 1.0f - z * z));
    float phi = TWO_PI * u.y;
    return {r * std::cos(phi), r * std::sin(phi), z};
}

/// PDF for uniform hemisphere sampling: 1 / (2π).
inline float uniformHemispherePdf() {
    return INV_TWO_PI;
}

/// Concentric disk mapping (Shirley-Chiu) — maps [0,1)^2 to unit disk.
// Naif yol (r = √u₁, θ = 2πu₂) da düzgündür ama kareyi çok bozar: yakın noktalar
// uzaklaşır, tabakalama (stratification) bozulur. Eşmerkezli eşleme [−1,1]² karesinin
// iç içe kare halkalarını iç içe çember halkalarına götürür; alan oranı korunur
// (pdf = 1/π, disk alanı π) ve bozulma azdır (Shirley & Chiu 1997, PBRT A.5.1).
inline Vec2f uniformSampleDisk(Vec2f u) {
    // Map [0,1) to [-1,1)
    float sx = 2.0f * u.x - 1.0f;
    float sy = 2.0f * u.y - 1.0f;

    // Handle origin
    if (sx == 0.0f && sy == 0.0f) {
        return {0.0f, 0.0f};
    }

    float r, theta;
    if (std::abs(sx) > std::abs(sy)) {
        r     = sx;
        theta = PI_OVER_4 * (sy / sx);
    } else {
        r     = sy;
        theta = PI_OVER_2 - PI_OVER_4 * (sx / sy);
    }

    return {r * std::cos(theta), r * std::sin(theta)};
}

/// Cosine-weighted hemisphere sampling using concentric disk mapping.
// Malley yöntemi: diskte düzgün nokta seç, yarıküreye dik olarak yukarı izdüşür
// (z = √(1 − x² − y²)). Disk üzerindeki alan elemanı dA = cosθ·dω olduğundan
// pdf_ω = pdf_A · cosθ = cosθ/π. Lambert (difüz) BRDF'de cosθ terimi pdf ile sadeleşir.
inline Vec3f cosineSampleHemisphere(Vec2f u) {
    Vec2f d = uniformSampleDisk(u);
    float z = std::sqrt(std::max(0.0f, 1.0f - d.x * d.x - d.y * d.y));
    return {d.x, d.y, z};
}

/// PDF for cosine-weighted hemisphere sampling: cosθ / π.
inline float cosineHemispherePdf(float cosTheta) {
    return cosTheta * INV_PI;
}

/// Uniformly sample a point on a triangle, returning barycentric coordinates (b1, b2).
/// The third barycentric coordinate is b0 = 1 - b1 - b2.
// Ters CDF: b1 = 1 − √u₁ marjinali, bir köşeden uzaklaştıkça kesit uzunluğunun doğrusal
// büyümesini telafi eder (√ olmadan noktalar o köşede kümelenirdi). Sonra o kesit boyunca
// u₂ ile doğrusal seçim (b2 = u₂·√u₁). Alan üzerinde düzgün → pdf = 1/Alan (PBRT 4. baskı, 6.5.4).
inline Vec2f uniformSampleTriangle(Vec2f u) {
    float su0 = std::sqrt(u.x);
    return {1.0f - su0, u.y * su0};
}

} // namespace photon
