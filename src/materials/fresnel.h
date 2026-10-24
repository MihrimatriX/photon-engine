// fresnel.h — Gelişmiş Fresnel modelleri: karmaşık indisli iletken (metal), F82-tint
// (OpenPBR'ın metal kenar tonu), Gulbrandsen'in sanatçı dostu metal ters çevirmesi ve
// ince film girişimi (Airy toplamı). Hepsi kutuplanmamış ışık için (s ve p ortalaması).
// Kaynaklar: Born & Wolf §1.5 ve §13.4; Gulbrandsen 2014 (JCGT 3(4)); Kutz, Hasan,
// Edmondson 2021 "Novel aspects of the Adobe Standard Material" (F82); Belcour & Barla
// 2017 "A Practical Extension to Microfacet Theory for the Modeling of Varying Iridescence".
#pragma once

#include "core/color/spectrum.h"
#include "core/math/constants.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace photon {

using cfloat = std::complex<float>;

/// Schlick (tek kanal).
inline float schlickF(float f0, float cosTheta) {
    const float m = 1.0f - std::clamp(cosTheta, 0.0f, 1.0f);
    const float m2 = m * m;
    return f0 + (1.0f - f0) * m2 * m2 * m;
}

/// F82-tint: Schlick eğrisi, μ̄ = 1/7 (~82°) açısında tint·Schlick değerine çekilir.
///   F(μ) = F_s(μ) − b·μ·(1−μ)⁶,   b = F_s(μ̄)(1 − t) / (μ̄ (1 − μ̄)⁶)
/// t = 1 → saf Schlick. Altın ve bakır gibi metallerin kenarda biraz koyulaşıp renk
/// değiştirmesini tek bir renk parametresiyle verir (OpenPBR "specular_color").
inline Color3f fresnelF82Tint(const Color3f& f0, const Color3f& tint, float cosTheta) {
    constexpr float muBar = 1.0f / 7.0f;
    constexpr float oneMinus = 1.0f - muBar;
    const float denom = muBar * oneMinus * oneMinus * oneMinus * oneMinus * oneMinus * oneMinus;
    const float mu = std::clamp(cosTheta, 0.0f, 1.0f);
    const float m = 1.0f - mu;
    const float m6 = m * m * m * m * m * m;
    Color3f out;
    for (int c = 0; c < 3; ++c) {
        const float fsBar = schlickF(f0[c], muBar);
        const float b = fsBar * (1.0f - tint[c]) / denom;
        out[c] = std::clamp(schlickF(f0[c], mu) - b * mu * m6, 0.0f, 1.0f);
    }
    return out;
}

/// Gulbrandsen 2014: yansıtma r (= F0) ve kenar tonu g'den metalin karmaşık indisi n + ik.
inline cfloat gulbrandsenIor(float r, float g) {
    r = std::clamp(r, 0.0f, 0.99f);
    g = std::clamp(g, 0.0f, 1.0f);
    const float sr = std::sqrt(r);
    const float nMin = (1.0f - r) / (1.0f + r);
    const float nMax = (1.0f + sr) / (1.0f - sr);
    const float n = g * nMin + (1.0f - g) * nMax;
    const float k2 = (r * (n + 1.0f) * (n + 1.0f) - (n - 1.0f) * (n - 1.0f)) / (1.0f - r);
    return {n, std::sqrt(std::max(0.0f, k2))};
}

/// İki ortam arası kutuplanmamış Fresnel yansıması; n2 karmaşık olabilir (metal).
/// cos1: birinci ortamda geliş açısının kosinüsü.
inline float fresnelComplex(float cos1, float n1, cfloat n2) {
    cos1 = std::clamp(cos1, 0.0f, 1.0f);
    const float sin1sq = 1.0f - cos1 * cos1;
    const cfloat cos2 = std::sqrt(cfloat(1.0f) - (n1 * n1 * sin1sq) / (n2 * n2));
    const cfloat rs = (n1 * cos1 - n2 * cos2) / (n1 * cos1 + n2 * cos2);
    const cfloat rp = (n2 * cos1 - n1 * cos2) / (n2 * cos1 + n1 * cos2);
    return std::clamp(0.5f * (std::norm(rs) + std::norm(rp)), 0.0f, 1.0f);
}

/// İnce film girişimi: ortam n1 → film (n2, kalınlık d nm) → taban n3 (karmaşık olabilir).
/// Film içinde sayısız kez yansıyan dalgaların toplamı (Airy):
///   r = (r12 + r23·e^{iδ}) / (1 + r12·r23·e^{iδ}),   δ = 4π n2 d cosθ2 / λ
/// d → 0'da r12 ve r23 birleşip doğrudan n1 → n3 Fresnel'ine iner.
inline float thinFilmReflectance(float cos1, float n1, float n2, cfloat n3, float thicknessNm, float lambdaNm) {
    cos1 = std::clamp(cos1, 0.0f, 1.0f);
    const float sin1sq = 1.0f - cos1 * cos1;
    const float sin2sq = (n1 / n2) * (n1 / n2) * sin1sq;
    if (sin2sq >= 1.0f) return 1.0f;
    const float cos2 = std::sqrt(1.0f - sin2sq);
    const cfloat cos3 = std::sqrt(cfloat(1.0f) - (n1 * n1 * sin1sq) / (n3 * n3));
    const float r12s = (n1 * cos1 - n2 * cos2) / (n1 * cos1 + n2 * cos2);
    const float r12p = (n2 * cos1 - n1 * cos2) / (n2 * cos1 + n1 * cos2);
    const cfloat r23s = (n2 * cos2 - n3 * cos3) / (n2 * cos2 + n3 * cos3);
    const cfloat r23p = (n3 * cos2 - n2 * cos3) / (n3 * cos2 + n2 * cos3);
    const float delta = 4.0f * PI * n2 * thicknessNm * cos2 / lambdaNm;
    const cfloat e = std::polar(1.0f, delta);
    const cfloat rs = (r12s + r23s * e) / (1.0f + r12s * r23s * e);
    const cfloat rp = (r12p + r23p * e) / (1.0f + r12p * r23p * e);
    return std::clamp(0.5f * (std::norm(rs) + std::norm(rp)), 0.0f, 1.0f);
}

/// Kanal başına üç dalga boyunun ortalaması (kalın filmlerde renk kırılmasını yumuşatır).
/// @p baseIor her kanal için tabanın (karmaşık) indisi.
inline Color3f thinFilmRGB(float cos1, float filmIor, const cfloat baseIor[3], float thicknessNm) {
    static constexpr float kLambda[3][3] = {{610.0f, 640.0f, 670.0f},
                                            {515.0f, 540.0f, 565.0f},
                                            {440.0f, 465.0f, 490.0f}};
    Color3f out;
    for (int c = 0; c < 3; ++c) {
        float s = 0.0f;
        for (float l : kLambda[c]) s += thinFilmReflectance(cos1, 1.0f, filmIor, baseIor[c], thicknessNm, l);
        out[c] = s / 3.0f;
    }
    return out;
}

} // namespace photon
