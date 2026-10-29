// eon.h — Enerji korunumlu Oren–Nayar difüz BRDF'si (EON; Portsmouth, Kutz, Hill 2025,
// JCGT 14(1), OpenPBR 1.1 ve MaterialX'in "base_diffuse_roughness" modeli).
//
// Pürüzlü difüz yüzeyler (kil, beton, kadife, ay yüzeyi) Lambert'ten daha "düz" görünür:
// ışığa bakan kenarlar kararmaz, ters tarafta geri saçılma olur. Klasik Oren–Nayar bunu tek
// saçılma için verir ama mikro-yüzeyler arası çoklu saçılmayı atar: beyaz yüzey enerji kaybeder.
// EON iki parçadır:
//   f_ss: Fujii'nin iyileştirilmiş Oren–Nayar'ı (FON), analitik albedosu E_FON(μ) bilinir;
//   f_ms: kaybolan (1 − E_FON) payını geri getiren, karşılıklı (reciprocal) bir telafi lobu.
// Sonuç: ρ = 1'de her açıda ve pürüzlülükte albedo tam 1. r = 0'da Lambert'e eşittir.
// Tüm vektörler yerel çerçevede (+z = normal), wi.z > 0 ve wo.z > 0 varsayılır.
#pragma once

#include "core/color/spectrum.h"
#include "core/math/constants.h"
#include "core/math/vec.h"

#include <algorithm>
#include <cmath>

namespace photon {

inline constexpr float kFonC1 = 0.5f - 2.0f / (3.0f * PI);
inline constexpr float kFonC2 = 2.0f / 3.0f - 28.0f / (15.0f * PI);

/// FON'un yönlü albedosu (ρ = 1), kapalı form (EON makalesi, denklem 9).
inline float fonAlbedo(float mu, float r) {
    mu = std::clamp(mu, 1e-4f, 1.0f);
    const float AF = 1.0f / (1.0f + kFonC1 * r);
    const float BF = r * AF;
    const float Si = std::sqrt(std::max(0.0f, 1.0f - mu * mu));
    const float G = Si * (std::acos(mu) - Si * mu) + (2.0f / 3.0f) * ((Si / mu) * (1.0f - Si * Si * Si) - Si);
    return AF + (BF * INV_PI) * G;
}

/// EON BRDF değeri. @p rho difüz renk, @p r pürüzlülük [0, 1].
inline Color3f eonEval(const Color3f& rho, float r, const Vec3f& wi, const Vec3f& wo) {
    const float muI = wi.z, muO = wo.z;
    if (r <= 0.0f) return rho * INV_PI;
    const float s = wi.dot(wo) - muI * muO;
    const float sOverT = s > 0.0f ? s / std::max(muI, muO) : s;
    const float AF = 1.0f / (1.0f + kFonC1 * r);
    const Color3f fss = rho * (INV_PI * AF * (1.0f + r * sOverT));
    const float EFo = fonAlbedo(muO, r), EFi = fonAlbedo(muI, r);
    const float avgEF = AF * (1.0f + kFonC2 * r);
    Color3f rhoMs;
    for (int c = 0; c < 3; ++c) rhoMs[c] = rho[c] * rho[c] * avgEF / std::max(1e-6f, 1.0f - rho[c] * (1.0f - avgEF));
    constexpr float eps = 1e-7f;
    const float ms = std::max(eps, 1.0f - EFo) * std::max(eps, 1.0f - EFi) / std::max(eps, 1.0f - avgEF);
    return fss + rhoMs * (INV_PI * ms);
}

} // namespace photon
