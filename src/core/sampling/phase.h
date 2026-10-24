// phase.h — Henyey–Greenstein faz fonksiyonu: katılımcı ortamda (sis, süt, mum, deri)
// bir saçılma olayında ışığın yeni yönünün dağılımı. Yüzeylerdeki BSDF'nin hacimdeki
// karşılığıdır; tek parametresi g ∈ (−1, 1): 0 her yöne eşit (izotrop), > 0 ileri,
// < 0 geri saçılma. Kaynak: Henyey & Greenstein 1941, PBRT 4. baskı §11.3.
#pragma once

#include "core/math/constants.h"
#include "core/math/frame.h"
#include "core/math/vec.h"

#include <algorithm>
#include <cmath>

namespace photon {

/// p(cosθ), θ = yayılma yönü d ile yeni yön d' arasındaki açı. ∫ p dω = 1.
///   p = (1 − g²) / (4π (1 + g² − 2g cosθ)^{3/2})
inline float henyeyGreenstein(float cosTheta, float g) {
    const float denom = std::max(1e-6f, 1.0f + g * g - 2.0f * g * cosTheta);
    return (1.0f - g * g) / (4.0f * PI * denom * std::sqrt(denom));
}

/// d (yayılma yönü, birim) etrafında HG dağılımından yeni yön. Faz fonksiyonu kendi
/// pdf'i olduğu için yol ağırlığı değişmez (p / pdf = 1).
/// Ters CDF: cosθ = (1 + g² − ((1 − g²) / (1 − g + 2g·u))²) / (2g); u = 0 → −1, u = 1 → +1.
inline Vec3f sampleHenyeyGreenstein(const Vec3f& d, float g, const Vec2f& u) {
    float cosTheta;
    if (std::abs(g) < 1e-3f) {
        cosTheta = 1.0f - 2.0f * u.x;
    } else {
        const float sq = (1.0f - g * g) / (1.0f - g + 2.0f * g * u.x);
        cosTheta = (1.0f + g * g - sq * sq) / (2.0f * g);
    }
    cosTheta = std::clamp(cosTheta, -1.0f, 1.0f);
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const float phi = TWO_PI * u.y;
    const Frame f(d);
    return f.toWorld(Vec3f(sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta)).normalized();
}

} // namespace photon
