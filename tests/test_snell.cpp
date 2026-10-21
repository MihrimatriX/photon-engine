// Snell yasası testleri: beklenen kırılma yönleri refractVec ÇAĞRILMADAN elle
// hesaplanır (eski test refractVec'i kendisiyle karşılaştırdığı için aynalanmış
// kırılmayı yakalayamamıştı, bulgu core-1).

#include <gtest/gtest.h>
#include "materials/dielectric.h"
#include "geometry/sphere.h"
#include "core/math/utils.h"
#include "core/random/rng.h"
#include <cmath>

using namespace photon;

namespace {

// Düz yüzey: geometrik ve gölgeleme normali +z (dışarı). Işın hangi taraftan gelirse
// gelsin SurfaceInteraction::normal ışına doğru çevrilir, ng dışa bakar.
SurfaceInteraction flatSurface(const Vec3f& wo) {
    SurfaceInteraction si;
    si.ng = Vec3f(0, 0, 1);
    si.frontFace = wo.z > 0.0f;
    si.normal = si.frontFace ? si.ng : -si.ng;
    si.tangent = Vec3f(1, 0, 0);
    return si;
}

Vec3f sampleRefraction(const Dielectric& glass, const Vec3f& wo, const SurfaceInteraction& si) {
    Vec3f wi;
    Color3f f;
    float pdf = 0.0f;
    // uc = 0.9999 > F → her zaman kırılma lobu (TIR yoksa).
    EXPECT_TRUE(glass.sampleWithLobe(wo, si, 0.9999f, Vec2f(0.5f, 0.5f), wi, f, pdf));
    return wi;
}

} // namespace

// 45° havadan 1.5 cama: sinθt = sin45°/1.5 = 0.47140, cosθt = 0.88192.
// Kırılan ışın yüzeyden aşağı iner ve teğet bileşeni wo'nunkinin TERSİ yöndedir
// (wo +x tarafında, yani ışık -x yönünde ilerliyor; kırılınca da -x yönünde ilerler).
TEST(Snell, FortyFiveDegreesIntoGlass) {
    const float s = std::sqrt(0.5f);
    Vec3f wo(s, 0.0f, s);
    Dielectric glass(1.5f, Color3f(1.0f));
    Vec3f wt = sampleRefraction(glass, wo, flatSurface(wo));

    const float sinT = s / 1.5f;
    const float cosT = std::sqrt(1.0f - sinT * sinT);
    EXPECT_NEAR(wt.x, -sinT, 1e-5f);
    EXPECT_NEAR(wt.y, 0.0f, 1e-5f);
    EXPECT_NEAR(wt.z, -cosT, 1e-5f);

    // Aynı sonuç refractVec'in PBRT kuralıyla da çıkmalı (wi yüzeyden DIŞARI bakar).
    Vec3f direct;
    ASSERT_TRUE(refractVec(wo, Vec3f(0, 0, 1), 1.0f / 1.5f, direct));
    EXPECT_NEAR(direct.x, -sinT, 1e-5f);
    EXPECT_NEAR(direct.z, -cosT, 1e-5f);
}

// Küre içinden çıkış: nokta (-0.15, 0.98869, 0), içeriden sinθi = 0.15,
// sinθt = 1.5·0.15 = 0.225. Teğet birim vektör t = (0.98869, 0.15, 0),
// wt = sinθt·t + cosθt·n = (0.07630, 0.99709, 0). Aynalanmış (eski) sonuç (-0.369, 0.930, 0) olurdu.
TEST(Snell, ExitingSphereBendsAwayFromNormal) {
    Dielectric glass(1.5f, Color3f(1.0f));
    Sphere sphere(Vec3f(0.0f), 1.0f, &glass);
    Ray ray(Vec3f(-0.15f, -0.2f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f));
    SurfaceInteraction isect;
    ASSERT_TRUE(sphere.intersect(ray, isect));

    Vec3f wt = sampleRefraction(glass, -ray.direction, isect);
    EXPECT_NEAR(wt.x, 0.07630f, 2e-4f);
    EXPECT_NEAR(wt.y, 0.99709f, 2e-4f);
    EXPECT_NEAR(wt.z, 0.0f, 1e-5f);
}

// Rastgele yönlerde Snell değişmezleri, her iki taraftan:
//   |wt × n| = eta·|wo × n|   (eta = η_wo / η_wt)
//   wt ile wo yüzeyin zıt taraflarında
//   teğet bileşenler zıt yönlü: (wt - (wt·n)n)·(wo - (wo·n)n) < 0
TEST(Snell, InvariantsBothSides) {
    Dielectric glass(1.5f, Color3f(1.0f));
    RNG rng(7);
    const Vec3f n(0, 0, 1);
    int checked = 0;
    for (int i = 0; i < 2000; ++i) {
        float z = 2.0f * rng.uniformFloat() - 1.0f;
        float phi = TWO_PI * rng.uniformFloat();
        float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        Vec3f wo(r * std::cos(phi), r * std::sin(phi), z);
        if (std::abs(z) < 0.05f) continue;
        bool entering = z > 0.0f;
        float eta = entering ? 1.0f / 1.5f : 1.5f;
        float sinO = wo.cross(n).length();
        if (eta * sinO >= 0.999f) continue; // TIR: kırılma yok
        if (sinO < 1e-3f) continue;         // dik geliş: teğet bileşen yok

        SurfaceInteraction si = flatSurface(wo);
        Vec3f wi;
        Color3f f;
        float pdf = 0.0f;
        ASSERT_TRUE(glass.sampleWithLobe(wo, si, 0.99999f, Vec2f(0.5f, 0.5f), wi, f, pdf));
        EXPECT_LT(wi.z * wo.z, 0.0f);
        EXPECT_NEAR(wi.cross(n).length(), eta * sinO, 1e-4f);
        Vec3f tanO = wo - n * wo.dot(n);
        Vec3f tanT = wi - n * wi.dot(n);
        EXPECT_LT(tanO.dot(tanT), 0.0f);
        ++checked;
    }
    EXPECT_GT(checked, 1000);
}
