// Metal ve kaplama testleri: çoklu saçılma telafisiyle beyaz pürüzlü metalin enerjiyi
// koruması (fırın testi), F82-tint'in Schlick'e indirgenmesi ve kenar değeri, ince filmin
// sıfır kalınlıkta doğrudan Fresnel'e inmesi, Gulbrandsen ters çevirmesi, metalik pullar.

#include <gtest/gtest.h>
#include "core/math/utils.h"
#include "materials/disney.h"
#include "materials/fresnel.h"
#include "materials/microfacet.h"
#include "samplers/independent_sampler.h"

#include <cmath>

using namespace photon;

namespace {

SurfaceInteraction flatSi() {
    SurfaceInteraction si;
    si.point = Vec3f(0.123f, 0.0f, 0.456f);
    si.normal = si.ng = Vec3f(0, 0, 1);
    si.tangent = Vec3f(1, 0, 0);
    return si;
}

/// Yönlü albedo ρ(wo) = E[f·|cos|/pdf], malzemenin kendi örneklemesiyle.
Color3f albedo(const Material& m, const SurfaceInteraction& si, const Vec3f& wo, int n, uint64_t seed = 21) {
    IndependentSampler rng(seed);
    Color3f sum(0.0f);
    for (int i = 0; i < n; ++i) {
        BSDFSample bs;
        if (!m.sampleFull(wo, si, rng.get1D(), rng.get2D(), bs) || bs.pdf <= 0.0f) continue;
        sum += bs.f * (std::abs(bs.wi.dot(si.normal)) / bs.pdf);
    }
    return sum / static_cast<float>(n);
}

Vec3f dirFromCos(float c) { return Vec3f(std::sqrt(1.0f - c * c), 0.0f, c); }

} // namespace

// Fırın testi: beyaz (F0 = 1) metal her pürüzlülükte ve açıda ışığın tamamını yansıtmalı.
// Telafisiz tek saçılmalı GGX'te r = 1'de bu değer ~0.6'ya düşer.
TEST(Metal, WhiteRoughMetalConservesEnergy) {
    const SurfaceInteraction si = flatSi();
    for (float r : {0.1f, 0.4f, 0.7f, 1.0f}) {
        DisneyMaterial m(Color3f(1.0f), 1.0f, r, 0.5f);
        for (float c : {1.0f, 0.6f, 0.25f}) {
            const Color3f a = albedo(m, si, dirFromCos(c), 60000);
            EXPECT_NEAR(a.g, 1.0f, 0.03f) << "roughness " << r << " cos " << c;
        }
    }
}

TEST(Metal, DirectionalAlbedoTableIsSane) {
    EXPECT_NEAR(ggxDirectionalAlbedo(1.0f, 0.0f), 1.0f, 0.01f);
    EXPECT_LT(ggxDirectionalAlbedo(0.5f, 1.0f), 0.8f);
    EXPECT_GT(ggxDirectionalAlbedo(0.5f, 1.0f), 0.4f);
    // Pürüzlülük arttıkça kayıp artar.
    EXPECT_GT(ggxDirectionalAlbedo(0.5f, 0.2f), ggxDirectionalAlbedo(0.5f, 0.8f));
}

TEST(Fresnel, F82TintReducesToSchlickAndHitsTarget) {
    const Color3f f0(0.95f, 0.64f, 0.54f);
    for (float c : {1.0f, 0.5f, 0.1f}) {
        const Color3f f = fresnelF82Tint(f0, Color3f(1.0f), c);
        EXPECT_NEAR(f.r, schlickF(f0.r, c), 1e-5f);
        EXPECT_NEAR(f.b, schlickF(f0.b, c), 1e-5f);
    }
    const Color3f tint(0.8f, 0.9f, 1.0f);
    const Color3f at82 = fresnelF82Tint(f0, tint, 1.0f / 7.0f);
    EXPECT_NEAR(at82.r, tint.r * schlickF(f0.r, 1.0f / 7.0f), 1e-4f);
    EXPECT_NEAR(at82.g, tint.g * schlickF(f0.g, 1.0f / 7.0f), 1e-4f);
    // Dik gelişte F0 korunur.
    EXPECT_NEAR(fresnelF82Tint(f0, tint, 1.0f).r, f0.r, 1e-5f);
}

TEST(Fresnel, ComplexMatchesDielectricForRealIor) {
    for (float c : {1.0f, 0.7f, 0.2f}) {
        EXPECT_NEAR(fresnelComplex(c, 1.0f, cfloat(1.5f, 0.0f)), fresnelDielectric(c, 1.0f, 1.5f), 1e-5f);
    }
}

TEST(Fresnel, GulbrandsenReproducesNormalReflectance) {
    for (float r : {0.04f, 0.5f, 0.9f}) {
        for (float g : {0.0f, 0.5f, 1.0f}) {
            const cfloat n = gulbrandsenIor(r, g);
            EXPECT_NEAR(fresnelComplex(1.0f, 1.0f, n), r, 2e-3f) << "r=" << r << " g=" << g;
        }
    }
}

TEST(Fresnel, ThinFilmVanishesAtZeroThickness) {
    const cfloat gold(0.18f, 3.0f);
    for (float c : {1.0f, 0.6f, 0.3f}) {
        EXPECT_NEAR(thinFilmReflectance(c, 1.0f, 1.33f, gold, 0.0f, 550.0f), fresnelComplex(c, 1.0f, gold), 1e-4f);
        EXPECT_NEAR(thinFilmReflectance(c, 1.0f, 1.33f, cfloat(1.5f), 0.0f, 550.0f), fresnelDielectric(c, 1.0f, 1.5f),
                    1e-4f);
    }
    // Sabun köpüğü (~ 380 nm, n = 1.33): dalga boyuna göre belirgin renk farkı.
    const cfloat air(1.0f);
    const float r = thinFilmReflectance(1.0f, 1.0f, 1.33f, air, 380.0f, 650.0f);
    const float b = thinFilmReflectance(1.0f, 1.0f, 1.33f, air, 380.0f, 450.0f);
    EXPECT_GT(std::abs(r - b), 0.02f);
}

TEST(Flakes, DensityControlsCoverageAndEnergyStaysBounded) {
    DisneyMaterial paint(Color3f(0.6f, 0.05f, 0.05f), 0.0f, 0.4f, 0.5f);
    paint.setClearCoat(1.0f);
    paint.setFlakes(0.6f, 0.01f, 0.3f, Color3f(0.95f));
    IndependentSampler rng(4);
    int in = 0;
    const int n = 4000;
    for (int i = 0; i < n; ++i) {
        SurfaceInteraction si = flatSi();
        si.point = Vec3f(rng.get1D(), rng.get1D(), 0.0f) * 10.0f;
        const auto p = paint.resolve(si);
        if (p.inFlake) {
            ++in;
            EXPECT_GT(p.flakeNormal.dot(si.normal), 0.0f);
        }
    }
    EXPECT_NEAR(static_cast<float>(in) / n, 0.6f, 0.06f);

    // Beyaz pul + kaplama: albedo 1'i aşmamalı (enerji yaratılmaz).
    DisneyMaterial white(Color3f(1.0f), 0.0f, 0.4f, 0.5f);
    white.setClearCoat(1.0f);
    white.setFlakes(1.0f, 0.01f, 0.0f, Color3f(1.0f));
    const Color3f a = albedo(white, flatSi(), dirFromCos(0.8f), 40000);
    EXPECT_LE(a.g, 1.02f);
    EXPECT_GT(a.g, 0.8f);
}
