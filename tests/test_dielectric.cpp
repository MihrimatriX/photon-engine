// Cam (Dielectric) malzeme testleri: delta cam yönleri, pürüzlü camın iç yansıması,
// pdf normalizasyonu (Jacobian kontrolü), enerji korunumu ve küçük pürüzlülükte taşma.

#include <gtest/gtest.h>
#include "materials/dielectric.h"
#include "materials/microfacet.h"
#include "geometry/sphere.h"
#include "core/math/frame.h"
#include "core/math/utils.h"
#include "core/random/rng.h"
#include "core/sampling/sampling.h"
#include <cmath>

using namespace photon;

namespace {

SurfaceInteraction flatSurface(const Vec3f& wo) {
    SurfaceInteraction si;
    si.ng = Vec3f(0, 0, 1);
    si.frontFace = wo.z > 0.0f;
    si.normal = si.frontFace ? si.ng : -si.ng;
    si.tangent = Vec3f(1, 0, 0);
    return si;
}

Vec3f dirFromAngle(float cosTheta, bool above) {
    float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    return Vec3f(sinTheta, 0.0f, above ? cosTheta : -cosTheta);
}

struct Moments {
    float pdfIntegral = 0.0f; ///< ∫ pdf dω (düzgün küre örneklemesiyle)
    float success = 0.0f;     ///< sample() başarı oranı
    float energy = 0.0f;      ///< E[f|cos|/pdf], kırılmada η² ölçeği geri alınmış
};

// pdf'in küre üzerindeki integrali, sample()'ın başarı oranına eşit olmalı
// (başarısız örnekler = arkası dönük mikro-yüzeyler, yatay altı yansımalar).
// Kırılma Jacobian'ı yanlışsa (ör. eta yerine 1/eta) integral belirgin sapar.
// İntegral (z, φ) üzerinde tabakalı ızgarayla alınır: dω = dz dφ, toplam alan 4π.
Moments measure(const Dielectric& glass, const Vec3f& wo, int n) {
    SurfaceInteraction si = flatSurface(wo);
    RNG rng(1234);
    Moments m;
    const int nz = 2000;
    const int nphi = 200;
    double integral = 0.0;
    for (int i = 0; i < nz; ++i) {
        for (int j = 0; j < nphi; ++j) {
            float z = 2.0f * (static_cast<float>(i) + rng.uniformFloat()) / nz - 1.0f;
            float phi = TWO_PI * (static_cast<float>(j) + rng.uniformFloat()) / nphi;
            float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
            integral += glass.pdf(wo, Vec3f(r * std::cos(phi), r * std::sin(phi), z), si);
        }
    }
    m.pdfIntegral = static_cast<float>(integral * (4.0 * PI) / (static_cast<double>(nz) * nphi));

    for (int i = 0; i < n; ++i) {
        Vec3f wi;
        Color3f f;
        float pdf = 0.0f;
        float uc = rng.uniformFloat();
        Vec2f u = rng.uniformFloat2D();
        if (glass.sampleWithLobe(wo, si, uc, u, wi, f, pdf)) {
            m.success += 1.0f;
            float weight = f.r * std::abs(wi.z) / pdf;
            if (wi.z * wo.z < 0.0f) {
                float etap = wo.z > 0.0f ? glass.ior() : 1.0f / glass.ior();
                weight *= etap * etap; // radyans ölçeklemesini geri al → enerji
            }
            m.energy += weight;
        }
    }
    m.success /= static_cast<float>(n);
    m.energy /= static_cast<float>(n);
    return m;
}

} // namespace

TEST(Dielectric, ExitRefractsTowardAir) {
    Dielectric glass(1.5f, Color3f(1.0f));
    Sphere sphere(Vec3f(0.0f), 1.0f, &glass);
    Ray ray(Vec3f(-0.15f, -0.2f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f));
    SurfaceInteraction isect;
    ASSERT_TRUE(sphere.intersect(ray, isect));

    Vec3f wo = (-ray.direction).normalized();
    Vec3f ng = isect.ng.normalized();
    EXPECT_GT(ng.dot(isect.point), 0.9f);
    EXPECT_LT(wo.dot(ng), 0.0f);
    EXPECT_LT(isect.normal.dot(ng), 0.0f);

    Vec3f wi;
    Color3f brdf;
    float pdf = 0.0f;
    ASSERT_TRUE(glass.sample(wo, isect, Vec2f(0.999f, 0.3f), wi, brdf, pdf));

    // Elle Snell (bkz. test_snell.cpp): çıkışta ışın normalden UZAKLAŞIR.
    // Eski test beklenen değeri refractVec(-woLocal, ...) ile hesaplıyordu, yani
    // aynalanmış kırılmayı (-0.369, 0.930, 0) "doğru" kabul ediyordu.
    EXPECT_NEAR(wi.x, 0.07630f, 2e-4f);
    EXPECT_NEAR(wi.y, 0.99709f, 2e-4f);
    EXPECT_NEAR(wi.z, 0.0f, 1e-4f);
    EXPECT_GT(wi.dot(ng), 0.0f);
}

TEST(Dielectric, FrostedSampleRefracts) {
    Dielectric frost(1.5f, Color3f(1.0f), 0.35f);
    SurfaceInteraction si;
    si.ng = Vec3f(0, 1, 0);
    si.normal = si.ng;
    Vec3f wo(0, 1, 0);
    Vec3f wi;
    Color3f brdf;
    float pdf = 0.0f;
    ASSERT_TRUE(frost.sample(wo, si, Vec2f(0.5f, 0.1f), wi, brdf, pdf));
    EXPECT_LT(wi.dot(si.ng), 0.0f);
    EXPECT_GT(pdf, 0.0f);
    Vec3f mirror(0, 1, 0);
    EXPECT_GT((wi - mirror).lengthSquared(), 1e-3f);
}

TEST(Dielectric, ClearGlassReflectsAtGrazing) {
    Dielectric glass(1.5f, Color3f(1.0f));
    SurfaceInteraction si;
    si.ng = Vec3f(0, 1, 0);
    si.normal = si.ng;
    Vec3f wo = Vec3f(1.0f, 0.02f, 0.0f).normalized();
    Vec3f wi;
    Color3f brdf;
    float pdf = 0.0f;
    ASSERT_TRUE(glass.sample(wo, si, Vec2f(0.5f, 0.2f), wi, brdf, pdf));
    EXPECT_GT(wi.dot(si.ng), 0.0f);
    EXPECT_GT(pdf, 0.5f);
}

// Delta kırılmanın ağırlığı f|cos|/pdf = eta² olmalı (materials-4):
// cama girerken 1/1.5², çıkarken 1.5².
TEST(Dielectric, SmoothRefractionScalesRadianceByEtaSquared) {
    Dielectric glass(1.5f, Color3f(1.0f));
    for (bool above : {true, false}) {
        Vec3f wo = dirFromAngle(0.9f, above);
        SurfaceInteraction si = flatSurface(wo);
        Vec3f wi;
        Color3f f;
        float pdf = 0.0f;
        ASSERT_TRUE(glass.sampleWithLobe(wo, si, 0.9999f, Vec2f(0.5f), wi, f, pdf));
        float weight = f.r * std::abs(wi.z) / pdf;
        float expected = above ? 1.0f / (1.5f * 1.5f) : 1.5f * 1.5f;
        EXPECT_NEAR(weight, expected, 1e-4f) << "above=" << above;
    }
}

// materials-2: camın İÇİNDE (wo.z < 0) pürüzlü yansıma artık siyah değil.
TEST(Dielectric, RoughInternalReflectionIsNotBlack) {
    Dielectric frost(1.5f, Color3f(1.0f), 0.3f);
    Vec3f wo = dirFromAngle(0.6f, false);           // içeride, θ ≈ 53° > kritik açı (41.8°)
    Vec3f wi(-wo.x, -wo.y, wo.z);                   // aynasal yansıma, yine içeride
    SurfaceInteraction si = flatSurface(wo);
    Color3f f = frost.eval(wo, wi, si);
    EXPECT_GT(f.r, 0.1f);
    EXPECT_GT(frost.pdf(wo, wi, si), 0.0f);
}

// pdf normalizasyonu + enerji: dışarıdan, içeriden ve TIR bölgesinden.
TEST(Dielectric, RoughPdfIntegratesAndConservesEnergy) {
    Dielectric frost(1.5f, Color3f(1.0f), 0.4f);
    const int n = 200000;
    for (float cosO : {0.95f, 0.5f}) {
        for (bool above : {true, false}) {
            Moments m = measure(frost, dirFromAngle(cosO, above), n);
            SCOPED_TRACE(testing::Message() << "cos=" << cosO << " above=" << above);
            EXPECT_NEAR(m.pdfIntegral, m.success, 0.03f);
            EXPECT_GT(m.success, 0.8f);
            // Tek saçılmalı mikro-yüzey biraz enerji kaybeder, asla kazanmaz.
            EXPECT_LE(m.energy, 1.01f);
            EXPECT_GT(m.energy, 0.85f);
        }
    }
}

// materials-7: çok küçük pürüzlülükte D taşmamalı, sonuçlar sonlu kalmalı.
TEST(Dielectric, TinyRoughnessStaysFinite) {
    EXPECT_TRUE(std::isfinite(ggxD(Vec3f(0, 0, 1), 1e-6f, 1e-6f)));
    EXPECT_TRUE(std::isfinite(ggxD(Vec3f(0.6f, 0.0f, 0.8f), 1e-6f, 1e-6f)));
    EXPECT_GT(ggxD(Vec3f(0, 0, 1), 0.01f, 0.01f), 1000.0f);

    Dielectric frost(1.5f, Color3f(1.0f), 0.01f); // alpha = 1e-4
    Vec3f wo = dirFromAngle(0.8f, true);
    SurfaceInteraction si = flatSurface(wo);
    RNG rng(3);
    for (int i = 0; i < 1000; ++i) {
        Vec3f wi;
        Color3f f;
        float pdf = 0.0f;
        if (!frost.sampleWithLobe(wo, si, rng.uniformFloat(), rng.uniformFloat2D(), wi, f, pdf)) continue;
        ASSERT_TRUE(f.isFinite());
        ASSERT_TRUE(std::isfinite(pdf));
        ASSERT_TRUE(std::isfinite(f.r * std::abs(wi.z) / pdf));
    }
}
