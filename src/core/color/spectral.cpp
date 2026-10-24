// spectral.cpp — CIE renk eşleme fonksiyonları, normalize RGB ağırlıkları ve dalga boyu örnekleme.
#include "core/color/spectral.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace photon {

namespace {

// Wyman, Sloan, Shirley 2013, "Simple Analytic Approximations to the CIE XYZ Color
// Matching Functions", JCGT 2(2). Parçalı Gauss: tepenin solunda σ1, sağında σ2.
float g(float x, float mu, float s1, float s2) {
    const float t = (x - mu) / (x < mu ? s1 : s2);
    return std::exp(-0.5f * t * t);
}

Color3f rawRGB(float l) {
    const float X = 1.056f * g(l, 599.8f, 37.9f, 31.0f) + 0.362f * g(l, 442.0f, 16.0f, 26.7f)
                  - 0.065f * g(l, 501.1f, 20.4f, 26.2f);
    const float Y = 0.821f * g(l, 568.8f, 46.9f, 40.5f) + 0.286f * g(l, 530.9f, 16.3f, 31.1f);
    const float Z = 1.217f * g(l, 437.0f, 11.8f, 36.0f) + 0.681f * g(l, 459.0f, 26.0f, 13.8f);
    // XYZ → doğrusal sRGB (D65), gamut dışı negatifler kırpılır.
    const float r = 3.2404542f * X - 1.5371385f * Y - 0.4985314f * Z;
    const float gg = -0.9692660f * X + 1.8760108f * Y + 0.0415560f * Z;
    const float b = 0.0556434f * X - 0.2040259f * Y + 1.0572252f * Z;
    return Color3f(std::max(0.0f, r), std::max(0.0f, gg), std::max(0.0f, b));
}

constexpr int kBins = 350; // 1 nm'lik kutular

struct Tables {
    Color3f norm{1.0f};                  // kanal başına normalize çarpanı
    std::array<float, kBins + 1> cdf{};  // r+g+b'ye orantılı, son = 1
    std::array<float, kBins> pdf{};      // kutu başına olasılık yoğunluğu (1/nm)

    Tables() {
        // 1) Kanal ortalamaları (ince adımlı orta nokta kuralı).
        Color3f sum(0.0f);
        constexpr int kSteps = 3500;
        const float dl = (kLambdaMax - kLambdaMin) / static_cast<float>(kSteps);
        for (int i = 0; i < kSteps; ++i) sum += rawRGB(kLambdaMin + (static_cast<float>(i) + 0.5f) * dl);
        const Color3f mean = sum / static_cast<float>(kSteps);
        norm = Color3f(1.0f / mean.r, 1.0f / mean.g, 1.0f / mean.b);
        // 2) Önem örneklemesi tablosu: s(λ) = r + g + b (normalize edilmiş).
        const float bw = (kLambdaMax - kLambdaMin) / static_cast<float>(kBins);
        float acc = 0.0f;
        cdf[0] = 0.0f;
        std::array<float, kBins> s{};
        for (int i = 0; i < kBins; ++i) {
            const Color3f c = rawRGB(kLambdaMin + (static_cast<float>(i) + 0.5f) * bw) * norm;
            // Küçük taban: kenarlardaki λ'lar da seçilebilsin (sıfır pdf'li ama sıfır olmayan
            // ağırlıklı bölge kalmasın).
            s[static_cast<size_t>(i)] = c.r + c.g + c.b + 0.02f;
            acc += s[static_cast<size_t>(i)];
            cdf[static_cast<size_t>(i) + 1] = acc;
        }
        for (int i = 0; i <= kBins; ++i) cdf[static_cast<size_t>(i)] /= acc;
        for (int i = 0; i < kBins; ++i) pdf[static_cast<size_t>(i)] = s[static_cast<size_t>(i)] / (acc * bw);
    }
};

const Tables& tables() {
    static const Tables t;
    return t;
}

} // namespace

Color3f wavelengthToRGB(float lambdaNm) {
    return rawRGB(lambdaNm) * tables().norm;
}

float sampleWavelength(float u, Color3f& weight) {
    const Tables& t = tables();
    const auto it = std::upper_bound(t.cdf.begin(), t.cdf.end(), u);
    int i = static_cast<int>(it - t.cdf.begin()) - 1;
    i = std::clamp(i, 0, kBins - 1);
    const float lo = t.cdf[static_cast<size_t>(i)], hi = t.cdf[static_cast<size_t>(i) + 1];
    const float f = hi > lo ? std::clamp((u - lo) / (hi - lo), 0.0f, 1.0f) : 0.5f;
    const float bw = (kLambdaMax - kLambdaMin) / static_cast<float>(kBins);
    const float lambda = kLambdaMin + (static_cast<float>(i) + f) * bw;
    // Monte Carlo: piksel_c = (1/aralık)∫ w_c L dλ ≈ w_c(λ) L(λ) / (aralık · p(λ)).
    weight = wavelengthToRGB(lambda) / ((kLambdaMax - kLambdaMin) * t.pdf[static_cast<size_t>(i)]);
    return lambda;
}

float cauchyIor(float nd, float abbe, float lambdaNm) {
    if (abbe <= 0.0f) return nd;
    // Fraunhofer çizgileri (μm): d = 587.56, F = 486.13, C = 656.27.
    constexpr float lD = 0.58756f, lF = 0.48613f, lC = 0.65627f;
    const float B = (nd - 1.0f) / (abbe * (1.0f / (lF * lF) - 1.0f / (lC * lC)));
    const float A = nd - B / (lD * lD);
    const float l = lambdaNm * 1e-3f;
    return A + B / (l * l);
}

} // namespace photon
