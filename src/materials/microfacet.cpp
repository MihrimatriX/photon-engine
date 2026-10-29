// microfacet.cpp — Ön hesaplı enerji tabloları: GGX yönlü albedosu E(μ, α) (çoklu saçılma
// telafisi), Schlick Fresnel'li ayrıştırması E_a / E_b ve ortalamaları (katman enerji
// korunumu), Charlie sheen albedosu E_sh(μ, r). Hepsi ilk kullanımda bir kez, deterministik
// tabakalı integrasyonla kurulur (birkaç on ms).
#include "materials/microfacet.h"

#include <array>

namespace photon {

namespace {

constexpr int kN = 32; // μ ve α (ya da r) ekseninde düğüm sayısı

using Table2D = std::array<float, kN * kN>;
using Table1D = std::array<float, kN>;

/// Düğümler i/(N−1) (uç noktalar dahil); μ satır içi (x), α satırlar (y). Çift doğrusal okuma.
float lookup2D(const Table2D& t, float mu, float alpha) {
    const float x = std::clamp(mu, 0.0f, 1.0f) * (kN - 1);
    const float y = std::clamp(alpha, 0.0f, 1.0f) * (kN - 1);
    const int x0 = std::clamp(static_cast<int>(std::floor(x)), 0, kN - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(y)), 0, kN - 1);
    const int x1 = std::min(x0 + 1, kN - 1), y1 = std::min(y0 + 1, kN - 1);
    const float fx = std::clamp(x - static_cast<float>(x0), 0.0f, 1.0f);
    const float fy = std::clamp(y - static_cast<float>(y0), 0.0f, 1.0f);
    auto at = [&](int ix, int iy) { return t[static_cast<size_t>(iy * kN + ix)]; };
    const float a = at(x0, y0) * (1.0f - fx) + at(x1, y0) * fx;
    const float b = at(x0, y1) * (1.0f - fx) + at(x1, y1) * fx;
    return a * (1.0f - fy) + b * fy;
}

float lookup1D(const Table1D& t, float alpha) {
    const float y = std::clamp(alpha, 0.0f, 1.0f) * (kN - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(y)), 0, kN - 1);
    const int y1 = std::min(y0 + 1, kN - 1);
    const float fy = std::clamp(y - static_cast<float>(y0), 0.0f, 1.0f);
    return t[static_cast<size_t>(y0)] * (1.0f - fy) + t[static_cast<size_t>(y1)] * fy;
}

float nodeMu(int im) { return std::max(static_cast<float>(im) / (kN - 1), 1e-3f); }

/// E(μo, α) = ∫ f_ss(wo, wi) cosθi dωi, F = 1 ile (tek saçılmalı GGX'in yansıttığı oran).
/// VNDF örneklemesiyle tahminci çok basitleşir: f·cos/pdf = G2(wo, wi) / G1(wo).
/// Aynı örneklerle Schlick ayrıştırması: c = wo·h için F = F0·(1 − (1−c)⁵) + (1−c)⁵.
struct GgxTables {
    Table2D e{}, ea{}, eb{};
    Table1D avg0{}, avg1{}, avg2{}; ///< E_avg(α, F0) = avg0 + F0·avg1 + F0²·avg2

    GgxTables() {
        constexpr int kS = 24;
        for (int ia = 0; ia < kN; ++ia) {
            const float alpha = static_cast<float>(ia) / (kN - 1);
            for (int im = 0; im < kN; ++im) {
                const float mu = nodeMu(im);
                const Vec3f wo(std::sqrt(std::max(0.0f, 1.0f - mu * mu)), 0.0f, mu);
                const float g1 = ggxG1(wo, alpha, alpha);
                double sum = 0.0, sumA = 0.0, sumB = 0.0;
                for (int sy = 0; sy < kS; ++sy) {
                    for (int sx = 0; sx < kS; ++sx) {
                        const float u1 = (static_cast<float>(sx) + 0.5f) / kS;
                        const float u2 = (static_cast<float>(sy) + 0.5f) / kS;
                        const Vec3f h = ggxSampleVisibleNormal(wo, alpha, alpha, u1, u2);
                        const float c = wo.dot(h);
                        const Vec3f wi = h * (2.0f * c) - wo;
                        if (wi.z <= 0.0f) continue;
                        const double w = ggxG(wo, wi, alpha, alpha) / g1;
                        const double s5 = pow5(1.0f - std::clamp(c, 0.0f, 1.0f));
                        sum += w;
                        sumA += w * (1.0 - s5);
                        sumB += w * s5;
                    }
                }
                const size_t k = static_cast<size_t>(ia * kN + im);
                e[k] = static_cast<float>(sum / (kS * kS));
                ea[k] = static_cast<float>(sumA / (kS * kS));
                eb[k] = static_cast<float>(sumB / (kS * kS));
            }
        }
        // Ortalamalar: 2∫ g(μ) μ dμ, orta nokta kuralıyla tablodan.
        constexpr int kM = 256;
        for (int ia = 0; ia < kN; ++ia) {
            const float alpha = static_cast<float>(ia) / (kN - 1);
            double a0 = 0.0, a1 = 0.0, a2 = 0.0;
            for (int i = 0; i < kM; ++i) {
                const float mu = (static_cast<float>(i) + 0.5f) / kM;
                const float E = std::clamp(lookup2D(e, mu, alpha), 1e-3f, 1.0f);
                const float m = 1.0f / E - 1.0f;
                const float A = lookup2D(ea, mu, alpha), B = lookup2D(eb, mu, alpha);
                const double w = 2.0 * mu / kM;
                a0 += w * B;
                a1 += w * (A + B * m);
                a2 += w * (A * m);
            }
            avg0[static_cast<size_t>(ia)] = static_cast<float>(a0);
            avg1[static_cast<size_t>(ia)] = static_cast<float>(a1);
            avg2[static_cast<size_t>(ia)] = static_cast<float>(a2);
        }
    }
};

const GgxTables& ggxTables() {
    static const GgxTables t;
    return t;
}

/// E_sh(μ, r) = ∫ D_charlie(h)·V(μi, μo)·cosθi dωi; (cosθ, φ) ızgarasında orta nokta kuralı.
struct SheenTable {
    Table2D e{};
    SheenTable() {
        constexpr int kC = 96, kP = 48;
        for (int ir = 0; ir < kN; ++ir) {
            const float r = std::max(static_cast<float>(ir) / (kN - 1), 0.02f);
            for (int im = 0; im < kN; ++im) {
                const float mu = nodeMu(im);
                const Vec3f wo(std::sqrt(std::max(0.0f, 1.0f - mu * mu)), 0.0f, mu);
                double sum = 0.0;
                for (int i = 0; i < kC; ++i) {
                    const float cz = (static_cast<float>(i) + 0.5f) / kC;
                    const float s = std::sqrt(std::max(0.0f, 1.0f - cz * cz));
                    for (int j = 0; j < kP; ++j) {
                        // φ simetrik: yalnız [0, π] taranır, iki katı alınır.
                        const float phi = PI * (static_cast<float>(j) + 0.5f) / kP;
                        const Vec3f wi(s * std::cos(phi), s * std::sin(phi), cz);
                        const Vec3f h = (wo + wi).normalized();
                        sum += charlieD(h.z, r) * charlieV(cz, mu) * cz;
                    }
                }
                e[static_cast<size_t>(ir * kN + im)] = static_cast<float>(sum * (1.0 / kC) * (TWO_PI / kP));
            }
        }
    }
};

const SheenTable& sheenTable() {
    static const SheenTable t;
    return t;
}

} // namespace

float ggxDirectionalAlbedo(float cosTheta, float alpha) {
    return std::clamp(lookup2D(ggxTables().e, cosTheta, alpha), 1e-3f, 1.0f);
}

float ggxSchlickAlbedo(float cosTheta, float alpha, float f0) {
    const GgxTables& t = ggxTables();
    const float E = std::clamp(lookup2D(t.e, cosTheta, alpha), 1e-3f, 1.0f);
    const float ss = f0 * lookup2D(t.ea, cosTheta, alpha) + lookup2D(t.eb, cosTheta, alpha);
    return std::clamp(ss * (1.0f + f0 * (1.0f / E - 1.0f)), 0.0f, 1.0f);
}

float ggxSchlickAverageAlbedo(float alpha, float f0) {
    const GgxTables& t = ggxTables();
    const float v = lookup1D(t.avg0, alpha) + f0 * lookup1D(t.avg1, alpha) + f0 * f0 * lookup1D(t.avg2, alpha);
    return std::clamp(v, 0.0f, 1.0f);
}

float charlieSheenAlbedo(float cosTheta, float roughness) {
    return std::max(lookup2D(sheenTable().e, cosTheta, roughness), 0.0f);
}

} // namespace photon
