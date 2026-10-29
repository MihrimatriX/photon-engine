// Işık seçici testleri: ışık BVH'sinin pmf'i bir olasılık dağılımı mı (toplam 1), örnekleme
// sıklıkları pmf ile uyumlu mu (MIS ağırlıkları buna dayanır), arkası dönük tek yüzlü ışık hiç
// seçilmiyor mu ve çok yayıcılı sahnede (LED şerit) eşit örnekte gürültü düşüyor mu.

#include <gtest/gtest.h>
#include "camera/perspective_camera.h"
#include "engine/embree_accel.h"
#include "engine/renderer.h"
#include "engine/scene.h"
#include "geometry/mesh.h"
#include "lights/area_light.h"
#include "lights/environment_light.h"
#include "lights/light_sampler.h"
#include "lights/point_light.h"
#include "materials/lambertian.h"
#include "samplers/independent_sampler.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace photon;

namespace {

/// Bir düzlemde dağınık, farklı parlaklıkta ve yönlerde küçük yayıcı üçgenler.
std::vector<std::shared_ptr<Light>> scatteredTriangles(int n, uint64_t seed) {
    IndependentSampler rng(seed);
    std::vector<std::shared_ptr<Light>> out;
    for (int i = 0; i < n; ++i) {
        const Vec3f c(rng.get1D() * 10.0f - 5.0f, rng.get1D() * 3.0f + 0.5f, rng.get1D() * 10.0f - 5.0f);
        const Vec3f a(rng.get1D() - 0.5f, rng.get1D() - 0.5f, rng.get1D() - 0.5f);
        const Vec3f b(rng.get1D() - 0.5f, rng.get1D() - 0.5f, rng.get1D() - 0.5f);
        const float L = 0.5f + 10.0f * rng.get1D();
        out.push_back(std::make_shared<TriangleLight>(c, c + a * 0.3f, c + b * 0.3f, Color3f(L)));
    }
    return out;
}

} // namespace

TEST(LightSampler, BvhPmfIsADistributionAndMatchesSampling) {
    auto owned = scatteredTriangles(300, 11);
    owned.push_back(std::make_shared<PointLight>(Vec3f(0, 4, 0), Color3f(30.0f)));
    std::vector<const Light*> lights;
    for (const auto& l : owned) lights.push_back(l.get());
    const EnvironmentLight env(nullptr, 0.0f, 0.3f);
    LightSampler ls;
    ls.build(lights, &env);
    ASSERT_TRUE(ls.usesBvh());

    IndependentSampler rng(3);
    for (int trial = 0; trial < 6; ++trial) {
        const Vec3f p(rng.get1D() * 8.0f - 4.0f, 0.0f, rng.get1D() * 8.0f - 4.0f);
        const Vec3f n(0, 1, 0);
        const LightSampler::Context ctx = ls.prepare(p, n);
        double total = 0.0;
        std::vector<double> pmf(static_cast<size_t>(ls.candidateCount()));
        for (int i = 0; i < ls.candidateCount(); ++i) total += pmf[static_cast<size_t>(i)] = ctx.pmf(i);
        EXPECT_NEAR(total, 1.0, 1e-3) << "trial " << trial;

        // Örnekleme: dönen pmf, pmf() ile aynı; sıklıklar pmf'e uyar.
        std::vector<int> hits(pmf.size(), 0);
        constexpr int N = 200000;
        int failed = 0;
        for (int k = 0; k < N; ++k) {
            float pk = 0.0f;
            const int i = ctx.sample(rng.get1D(), pk);
            if (i < 0) {
                ++failed;
                continue;
            }
            ++hits[static_cast<size_t>(i)];
            if ((k & 255) == 0) EXPECT_NEAR(pk, pmf[static_cast<size_t>(i)], 1e-4 + 1e-3 * pmf[static_cast<size_t>(i)]);
        }
        // Seçilemeyen olasılık kütlesi = 1 − Σpmf (burada ~0).
        EXPECT_LT(failed, N / 500);
        double chi2 = 0.0;
        int dof = 0;
        for (size_t i = 0; i < pmf.size(); ++i) {
            const double e = pmf[i] * N;
            if (e < 5.0) continue;
            chi2 += (hits[i] - e) * (hits[i] - e) / e;
            ++dof;
        }
        // dof ~ 300: χ² ortalaması dof, std √(2·dof); 5σ üstü kesin uyumsuzluk.
        EXPECT_LT(chi2, dof + 5.0 * std::sqrt(2.0 * dof)) << "trial " << trial;
    }
}

TEST(LightSampler, OneSidedSpotBehindIsNeverPicked) {
    LightBounds lb;
    lb.bounds = AABB(Vec3f(0.0f));
    lb.w = Vec3f(0, -1, 0); // aşağı bakan dar spot
    lb.phi = 10.0f;
    lb.cosThetaO = std::cos(0.3f);
    lb.cosThetaE = 1.0f;
    EXPECT_GT(lb.importance(Vec3f(0, -2, 0), Vec3f(0, 1, 0)), 0.0f);
    EXPECT_EQ(lb.importance(Vec3f(0, 2, 0), Vec3f(0, -1, 0)), 0.0f);
    EXPECT_EQ(lb.importance(Vec3f(3, -0.5f, 0), Vec3f(0, 1, 0)), 0.0f); // koninin dışında
}

// LED şerit: 2000 yayıcı üçgen. Işık BVH'si yakındakileri seçer; aynı örnek sayısında düzgün
// seçime göre gürültü belirgin düşer, ortalama (yansızlık) korunur.
TEST(LightSampler, ManyEmittersRenderCleanerThanUniform) {
    Scene::setPreferOwnBvh(true);
    static const Lambertian floorMat(Color3f(0.7f));
    static const Lambertian led(Color3f(0.0f), Color3f(4.0f));
    auto build = [](Scene& scene) {
        const Vec3f up(0, 1, 0);
        std::vector<Vec3f> fp = {{-6, 0, -6}, {6, 0, -6}, {6, 0, 6}, {-6, 0, 6}};
        scene.addShape(std::make_shared<TriangleMesh>(fp, std::vector<Vec3f>{up, up, up, up}, std::vector<Vec2f>{},
                                                      std::vector<uint32_t>{0, 2, 1, 0, 3, 2}, &floorMat));
        // Spiral LED şerit: 1000 küçük dörtgen = 2000 üçgen.
        std::vector<Vec3f> p;
        std::vector<uint32_t> idx;
        for (int i = 0; i < 1000; ++i) {
            const float t = static_cast<float>(i) / 1000.0f;
            const float a = t * 25.0f, r = 0.5f + 4.5f * t;
            const Vec3f c(r * std::cos(a), 0.6f, r * std::sin(a));
            const uint32_t b = static_cast<uint32_t>(p.size());
            p.insert(p.end(), {c + Vec3f(-0.03f, 0, -0.03f), c + Vec3f(0.03f, 0, -0.03f), c + Vec3f(0.03f, 0, 0.03f),
                               c + Vec3f(-0.03f, 0, 0.03f)});
            idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
        }
        scene.addShape(std::make_shared<TriangleMesh>(p, std::vector<Vec3f>{}, std::vector<Vec2f>{}, idx, &led));
        scene.buildAccelerator();
    };
    Scene scene;
    build(scene);
    ASSERT_TRUE(scene.lightSampler().usesBvh());
    PerspectiveCamera cam(Vec3f(0, 7, 7), Vec3f(0, 0, 0), Vec3f(0, 1, 0), 50.0f, 1.0f);
    RenderSettings rs;
    rs.width = rs.height = 48;
    rs.maxBounces = 1;
    rs.filter = PixelFilter{PixelFilterType::Box, 1.0f};
    Renderer r;
    rs.samplesPerPixel = 512;
    const Image ref = r.render(scene, cam, rs);
    rs.samplesPerPixel = 8;
    const Image bvh = r.render(scene, cam, rs);

    // Aynı sahne, düzgün ışık seçimiyle (eski davranış: 33'ten çok ışıkta düzgün).
    LightSampler::setForceUniform(true);
    Scene uniformScene;
    build(uniformScene);
    LightSampler::setForceUniform(false);
    const Image uni = r.render(uniformScene, cam, rs);

    auto relMse = [](const Image& a, const Image& b) {
        double s = 0.0;
        for (int y = 0; y < a.height(); ++y)
            for (int x = 0; x < a.width(); ++x) {
                const float d = a.getPixel(x, y).luminance() - b.getPixel(x, y).luminance();
                const float rr = b.getPixel(x, y).luminance();
                s += d * d / (rr * rr + 0.01f);
            }
        return s / (a.width() * a.height());
    };
    auto mean = [](const Image& a) {
        double s = 0.0;
        for (int y = 0; y < a.height(); ++y)
            for (int x = 0; x < a.width(); ++x) s += a.getPixel(x, y).luminance();
        return s / (a.width() * a.height());
    };
    EXPECT_NEAR(mean(bvh) / mean(ref), 1.0, 0.05);
    EXPECT_NEAR(mean(uni) / mean(ref), 1.0, 0.05);
    const double errBvh = relMse(bvh, ref), errUni = relMse(uni, ref);
    std::printf("LED strip @8spp relMSE: light BVH %.4f, uniform %.4f\n", errBvh, errUni);
    EXPECT_LT(errBvh, 0.5 * errUni);
    Scene::setPreferOwnBvh(false);
}

// Yayıcı üçgenlerin MIS ağırlıkları isabet edilen üçgenin indeksiyle (primIndex) bulunur.
// Embree yolu bu indeksi kendi primID'sinden verir: iki kesişim arka ucu aynı ortalamayı
// vermeli (bir taraf yanlış ışığın pdf'ini kullansaydı yayıcıların yakını kararır ya da parlardı).
TEST(LightSampler, EmitterMisMatchesAcrossBackends) {
    if (!EmbreeAccel::available()) GTEST_SKIP() << "Embree yok";
    static const Lambertian floorMat(Color3f(0.7f));
    static const Lambertian led(Color3f(0.0f), Color3f(5.0f));
    auto render = [](bool own) {
        Scene::setPreferOwnBvh(own);
        Scene scene;
        const Vec3f up(0, 1, 0);
        std::vector<Vec3f> fp = {{-3, 0, -3}, {3, 0, -3}, {3, 0, 3}, {-3, 0, 3}};
        scene.addShape(std::make_shared<TriangleMesh>(fp, std::vector<Vec3f>{up, up, up, up}, std::vector<Vec2f>{},
                                                      std::vector<uint32_t>{0, 2, 1, 0, 3, 2}, &floorMat));
        // Farklı boyda 40 yayıcı üçgen: yanlış indeks farklı alan → farklı pdf demek.
        std::vector<Vec3f> p;
        std::vector<uint32_t> idx;
        for (int i = 0; i < 40; ++i) {
            const float a = 0.157f * static_cast<float>(i), r = 1.0f + 0.04f * static_cast<float>(i);
            const float s = 0.03f + 0.01f * static_cast<float>(i % 7);
            const Vec3f c(r * std::cos(a), 0.4f, r * std::sin(a));
            const uint32_t b = static_cast<uint32_t>(p.size());
            p.insert(p.end(), {c + Vec3f(-s, 0, -s), c + Vec3f(s, 0, -s), c + Vec3f(0, 0, s)});
            idx.insert(idx.end(), {b, b + 1, b + 2});
        }
        scene.addShape(std::make_shared<TriangleMesh>(p, std::vector<Vec3f>{}, std::vector<Vec2f>{}, idx, &led));
        scene.buildAccelerator();
        EXPECT_EQ(scene.usesEmbree(), !own);
        PerspectiveCamera cam(Vec3f(0, 4, 4), Vec3f(0, 0, 0), Vec3f(0, 1, 0), 50.0f, 1.0f);
        RenderSettings rs;
        rs.width = rs.height = 48;
        rs.samplesPerPixel = 256;
        rs.maxBounces = 2;
        Renderer r;
        const Image img = r.render(scene, cam, rs);
        double s = 0.0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) s += img.getPixel(x, y).luminance();
        return s / (img.width() * img.height());
    };
    const double own = render(true), embree = render(false);
    Scene::setPreferOwnBvh(false);
    EXPECT_NEAR(embree / own, 1.0, 0.02);
}
