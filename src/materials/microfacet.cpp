// microfacet.cpp — GGX yönlü albedo tablosu E(μ, α): çoklu saçılma enerji telafisi için.
#include "materials/microfacet.h"

#include <array>

namespace photon {

namespace {

constexpr int kN = 32; // μ ve α ekseninde hücre sayısı

/// E(μo, α) = ∫ f_ss(wo, wi) cosθi dωi, F = 1 ile (tek saçılmalı GGX'in yansıttığı oran).
/// VNDF örneklemesiyle tahminci çok basitleşir: f·cos/pdf = G2(wo, wi) / G1(wo).
/// 32×32 hücre × 256 tabakalı örnek, ilk kullanımda bir kez (~ms).
struct AlbedoTable {
    std::array<float, kN * kN> e{};
    AlbedoTable() {
        constexpr int kS = 16;
        for (int ia = 0; ia < kN; ++ia) {
            // Izgara uç noktaları içerir (α = 0..1, μ = 0..1): okumada dış değerleme yok.
            const float alpha = static_cast<float>(ia) / (kN - 1);
            for (int im = 0; im < kN; ++im) {
                const float mu = std::max(static_cast<float>(im) / (kN - 1), 1e-3f);
                const Vec3f wo(std::sqrt(std::max(0.0f, 1.0f - mu * mu)), 0.0f, mu);
                const float g1 = ggxG1(wo, alpha, alpha);
                double sum = 0.0;
                for (int sy = 0; sy < kS; ++sy) {
                    for (int sx = 0; sx < kS; ++sx) {
                        const float u1 = (static_cast<float>(sx) + 0.5f) / kS;
                        const float u2 = (static_cast<float>(sy) + 0.5f) / kS;
                        const Vec3f h = ggxSampleVisibleNormal(wo, alpha, alpha, u1, u2);
                        const Vec3f wi = h * (2.0f * wo.dot(h)) - wo;
                        if (wi.z <= 0.0f) continue;
                        sum += ggxG(wo, wi, alpha, alpha) / g1;
                    }
                }
                e[static_cast<size_t>(ia * kN + im)] = static_cast<float>(sum / (kS * kS));
            }
        }
    }
};

const AlbedoTable& table() {
    static const AlbedoTable t;
    return t;
}

} // namespace

float ggxDirectionalAlbedo(float cosTheta, float alpha) {
    const AlbedoTable& t = table();
    // Düğümler i/(N−1); çift doğrusal okuma.
    const float x = std::clamp(cosTheta, 0.0f, 1.0f) * (kN - 1);
    const float y = std::clamp(alpha, 0.0f, 1.0f) * (kN - 1);
    const int x0 = std::clamp(static_cast<int>(std::floor(x)), 0, kN - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(y)), 0, kN - 1);
    const int x1 = std::min(x0 + 1, kN - 1), y1 = std::min(y0 + 1, kN - 1);
    const float fx = std::clamp(x - static_cast<float>(x0), 0.0f, 1.0f);
    const float fy = std::clamp(y - static_cast<float>(y0), 0.0f, 1.0f);
    auto at = [&](int ix, int iy) { return t.e[static_cast<size_t>(iy * kN + ix)]; };
    const float a = at(x0, y0) * (1.0f - fx) + at(x1, y0) * fx;
    const float b = at(x0, y1) * (1.0f - fx) + at(x1, y1) * fx;
    return std::clamp(a * (1.0f - fy) + b * fy, 1e-3f, 1.0f);
}

} // namespace photon
