// Disney malzeme testleri: clearcoat enerji korunumu, Burley F0, küçük pürüzlülükte
// sonlu değerler, VNDF örnekleme ile pdf tutarlılığı ve doku/parametre çözümü.

#include <gtest/gtest.h>
#include "materials/disney.h"
#include "core/image/image.h"
#include "core/math/constants.h"
#include "core/random/rng.h"
#include "geometry/surface_interaction.h"
#include <algorithm>
#include <cmath>

using namespace photon;

TEST(ImageSampling, BilinearWrap) {
    Image img(2, 2);
    img.setPixel(0, 0, Color3f(1, 0, 0));
    img.setPixel(1, 0, Color3f(0, 1, 0));
    img.setPixel(0, 1, Color3f(0, 0, 1));
    img.setPixel(1, 1, Color3f(1, 1, 1));

    // Center of 2×2 → blend all four texels
    Color3f c = img.sampleBilinear(0.5f, 0.5f);
    EXPECT_GT(c.r, 0.2f);
    EXPECT_GT(c.g, 0.2f);
    EXPECT_GT(c.b, 0.2f);
    // Wrap: u=1.5 equals u=0.5
    Color3f c2 = img.sampleBilinear(1.5f, 0.5f);
    EXPECT_NEAR(c.r, c2.r, 1e-5f);
    EXPECT_NEAR(c.g, c2.g, 1e-5f);
    EXPECT_NEAR(c.b, c2.b, 1e-5f);
}

namespace {

SurfaceInteraction zUp() {
    SurfaceInteraction si;
    si.normal = Vec3f(0, 0, 1);
    si.ng = si.normal;
    si.tangent = Vec3f(1, 0, 0);
    si.uv = Vec2f(0.5f, 0.5f);
    return si;
}

/// Yönsel albedo A(wo) = ∫ f |cos| dωi, sample() ile Monte Carlo tahmini.
float albedo(const DisneyMaterial& mat, const Vec3f& wo, int n) {
    SurfaceInteraction si = zUp();
    RNG rng(99);
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        Vec3f wi;
        Color3f f;
        float pdf = 0.0f;
        if (mat.sample(wo, si, rng.uniformFloat2D(), wi, f, pdf) && pdf > 0.0f) {
            sum += f.r * std::abs(wi.z) / pdf;
        }
    }
    return static_cast<float>(sum / n);
}

} // namespace

// Eski test "ClearCoatAddsEnergy" idi ve kaplamanın HER yönde değeri artırmasını
// bekliyordu. Enerji korunan kaplamada (materials-10) taban (1 - Fc) ile zayıflar,
// bu yüzden aynasal tepe dışında değer azalabilir. Artık iki şeyi test ediyoruz:
// aynasal yönde parlama eklenir ve yatay açıda toplam enerji artmaz.
TEST(DisneyMaterial, ClearCoatAddsHighlightWithoutEnergyGain) {
    DisneyMaterial mat(Color3f(0.8f), 0.0f, 0.3f, 0.5f);
    SurfaceInteraction si = zUp();
    Vec3f wo = Vec3f(0.3f, 0.0f, 0.954f).normalized();
    Vec3f mirror(-wo.x, -wo.y, wo.z);

    Color3f base = mat.eval(wo, mirror, si);
    mat.setClearCoat(1.0f);
    mat.setClearCoatRoughness(0.05f);
    Color3f coated = mat.eval(wo, mirror, si);
    EXPECT_GT(coated.r, base.r);

    DisneyMaterial white(Color3f(1.0f), 0.0f, 0.5f, 0.5f);
    for (float cosO : {0.9f, 0.15f}) {
        Vec3f w(std::sqrt(1.0f - cosO * cosO), 0.0f, cosO);
        white.setClearCoat(0.0f);
        float aBase = albedo(white, w, 200000);
        white.setClearCoat(1.0f);
        white.setClearCoatRoughness(0.1f);
        float aCoat = albedo(white, w, 200000);
        // Kaplama yalnızca (1-Fc) ile tabanı karıştırır: sonuç max(taban, 1)'i aşmamalı.
        EXPECT_LE(aCoat, std::max(aBase, 1.0f) + 0.02f) << "cosO=" << cosO;
        EXPECT_GT(aCoat, 0.5f);
    }
}

// materials-9: dielektrik F0 = 0.08·specular. Siyah taban, dik bakış ve wi = wo'da
// f = F0·D(z)·G / 4, D(z) = 1/(π α²), G = 1 → F0 = 4π α² f.
TEST(DisneyMaterial, SpecularF0IsBurley) {
    DisneyMaterial mat(Color3f(0.0f), 0.0f, 0.2f, 0.5f);
    SurfaceInteraction si = zUp();
    Vec3f n(0, 0, 1);
    float alpha = 0.2f * 0.2f;
    float f = mat.eval(n, n, si).r;
    EXPECT_NEAR(4.0f * PI * alpha * alpha * f, 0.04f, 1e-3f);
}

// materials-7: roughness 0.001 (α = 1e-6) eskiden D = inf → NaN üretiyordu.
TEST(DisneyMaterial, TinyRoughnessStaysFinite) {
    DisneyMaterial mat(Color3f(0.9f), 1.0f, 0.001f, 0.5f);
    SurfaceInteraction si = zUp();
    Vec3f wo = Vec3f(0.3f, 0.2f, 0.93f).normalized();
    Vec3f mirror(-wo.x, -wo.y, wo.z);
    EXPECT_TRUE(mat.eval(wo, mirror, si).isFinite());
    EXPECT_TRUE(std::isfinite(mat.pdf(wo, mirror, si)));
    RNG rng(5);
    for (int i = 0; i < 1000; ++i) {
        Vec3f wi;
        Color3f f;
        float pdf = 0.0f;
        if (!mat.sample(wo, si, rng.uniformFloat2D(), wi, f, pdf)) continue;
        ASSERT_TRUE(std::isfinite(f.r * std::abs(wi.z) / pdf));
    }
}

// VNDF örneklemesi ile pdf() tutarlı mı: pdf'in küre integrali = sample() başarı oranı,
// ve sample()'ın döndürdüğü pdf, pdf() ile aynı.
TEST(DisneyMaterial, SamplePdfMatchesPdf) {
    DisneyMaterial mat(Color3f(0.7f, 0.5f, 0.3f), 0.6f, 0.35f, 0.5f);
    mat.setAnisotropy(0.6f);
    mat.setClearCoat(0.5f);
    mat.setClearCoatRoughness(0.2f);
    SurfaceInteraction si = zUp();
    Vec3f wo = Vec3f(0.5f, 0.3f, 0.81f).normalized();

    RNG rng(17);
    const int nz = 1000;
    const int nphi = 400;
    double integral = 0.0;
    for (int i = 0; i < nz; ++i) {
        for (int j = 0; j < nphi; ++j) {
            float z = (static_cast<float>(i) + rng.uniformFloat()) / nz; // üst yarıküre
            float phi = TWO_PI * (static_cast<float>(j) + rng.uniformFloat()) / nphi;
            float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
            integral += mat.pdf(wo, Vec3f(r * std::cos(phi), r * std::sin(phi), z), si);
        }
    }
    integral *= TWO_PI / (static_cast<double>(nz) * nphi);

    int ok = 0;
    const int n = 100000;
    for (int i = 0; i < n; ++i) {
        Vec3f wi;
        Color3f f;
        float pdf = 0.0f;
        if (!mat.sample(wo, si, rng.uniformFloat2D(), wi, f, pdf)) continue;
        ++ok;
        if (i % 97 == 0) {
            EXPECT_NEAR(pdf, mat.pdf(wo, wi, si), 1e-3f * std::max(1.0f, pdf));
        }
    }
    EXPECT_NEAR(integral, static_cast<double>(ok) / n, 0.02);
}

TEST(DisneyMaterial, AnisotropyFollowsTangent) {
    DisneyMaterial mat(Color3f(1.0f), 1.0f, 0.4f, 0.5f);
    mat.setAnisotropy(0.9f);
    SurfaceInteraction si;
    si.normal = Vec3f(0, 1, 0);
    si.ng = si.normal;
    si.uv = Vec2f(0, 0);
    Vec3f wo(0, 1, 0);
    Vec3f wi = Vec3f(0.6f, 0.8f, 0.0f).normalized();

    si.tangent = Vec3f(1, 0, 0);
    float along = mat.eval(wo, wi, si).luminance();
    si.tangent = Vec3f(0, 0, 1);
    float across = mat.eval(wo, wi, si).luminance();
    EXPECT_GT(std::abs(along - across), 1e-4f);
}

TEST(DisneyMaterial, SheenAddsAtWideHalfAngle) {
    DisneyMaterial mat(Color3f(0.2f, 0.3f, 0.8f), 0.0f, 0.8f, 0.3f);
    SurfaceInteraction si;
    si.normal = Vec3f(0, 1, 0);
    si.tangent = Vec3f(1, 0, 0);
    Vec3f wo = Vec3f(0.95f, 0.1f, 0.0f).normalized();
    Vec3f wi = Vec3f(-0.95f, 0.1f, 0.0f).normalized();
    mat.setSheen(0.0f);
    float plain = mat.eval(wo, wi, si).luminance();
    mat.setSheen(1.0f);
    float cloth = mat.eval(wo, wi, si).luminance();
    EXPECT_GT(cloth, plain);
}

TEST(DisneyMaterial, DiffuseTransmissionGoesThrough) {
    DisneyMaterial mat(Color3f(0.8f), 0.0f, 0.5f, 0.5f);
    mat.setDiffuseTransmission(1.0f);
    SurfaceInteraction si;
    si.normal = Vec3f(0, 1, 0);
    si.tangent = Vec3f(1, 0, 0);
    Vec3f wo(0, 1, 0);
    Vec3f wi;
    Color3f brdf;
    float pdf = 0.0f;
    ASSERT_TRUE(mat.sample(wo, si, Vec2f(0.8f, 0.3f), wi, brdf, pdf));
    EXPECT_LT(wi.dot(si.normal), 0.0f);
    EXPECT_GT(pdf, 0.0f);
}

TEST(DisneyMaterial, ResolveUsesBaseWithoutMaps) {
    DisneyMaterial mat(Color3f(0.2f, 0.4f, 0.6f), 0.7f, 0.25f, 0.5f);
    SurfaceInteraction si;
    si.normal = Vec3f(0, 1, 0);
    si.uv = Vec2f(0, 0);
    auto p = mat.resolve(si);
    EXPECT_NEAR(p.baseColor.r, 0.2f, 1e-5f);
    EXPECT_NEAR(p.metallic, 0.7f, 1e-5f);
    EXPECT_NEAR(p.roughness, 0.25f, 1e-5f);
    EXPECT_NEAR(p.normal.y, 1.0f, 1e-5f);
}
