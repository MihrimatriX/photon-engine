// test_environment.cpp — HDRI ortam ışığı testleri: önem örnekleme dağılımının integrali 1,
// düz beyaz haritada pdf = 1/4π, parlak bölgenin sık seçilmesi, döndürmede sample/pdf
// tutarlılığı ve kutuplarda yatay sarma (wrap) olmaması. pdf tutarsızsa ortam ışığı yanlış
// parlaklıkta katkı verir.
#include <gtest/gtest.h>
#include "camera/perspective_camera.h"
#include "core/image/image_io.h"
#include "engine/renderer.h"
#include "engine/scene.h"
#include "geometry/mesh.h"
#include "geometry/sphere.h"
#include "materials/disney.h"
#include "materials/lambertian.h"
#include "lights/environment_light.h"
#include "core/sampling/sampling.h"
#include "geometry/surface_interaction.h"
#include "samplers/independent_sampler.h"
#include <cstdio>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace photon;

namespace {

Image whiteMap(int w, int h) {
    Image img(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) img.setPixel(x, y, Color3f(1.0f));
    return img;
}

} // namespace

TEST(EnvironmentLight, CdfIntegratesToOne) {
    Image img = whiteMap(32, 16);
    EnvironmentLight env(std::make_shared<const Image>(img), 0.0f, 1.0f);
    EXPECT_NEAR(env.distributionIntegral(), 1.0f, 1e-4f);

    // Constant luminance + sin(theta) weight is uniform over the sphere.
    EXPECT_NEAR(env.pdfLi(Vec3f(1.0f, 0.0f, 0.0f)), uniformSpherePdf(), 0.02f);
    EXPECT_NEAR(env.pdfLi(Vec3f(0.0f, 0.0f, 1.0f)), uniformSpherePdf(), 0.02f);
    EXPECT_NEAR(env.pdfLi(Vec3f(0.0f, 0.0f, -1.0f), Vec3f(0, 1, 0)), uniformSpherePdf(), 0.02f);

    SurfaceInteraction si;
    si.normal = Vec3f(0, 1, 0);
    LightSample ls = env.sampleLi(si, Vec2f(0.37f, 0.62f));
    EXPECT_GT(ls.pdf, 0.0f);
    EXPECT_NEAR(ls.pdf, env.pdfLi(ls.wi), std::max(1e-3f, ls.pdf * 0.15f));
}

TEST(EnvironmentLight, SamplesBrightWindow) {
    constexpr int W = 32, H = 16;
    Image img(W, H);
    for (int y = 4; y < 10; ++y)
        for (int x = 8; x < 24; ++x) img.setPixel(x, y, Color3f(80.0f));

    EnvironmentLight env(std::make_shared<const Image>(img), 0.0f, 1.0f);
    EXPECT_NEAR(env.distributionIntegral(), 1.0f, 1e-4f);

    SurfaceInteraction si;
    si.normal = Vec3f(0, 1, 0);
    constexpr int N = 16;
    int inside = 0;
    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
            LightSample ls = env.sampleLi(si, Vec2f((i + 0.5f) / N, (j + 0.5f) / N));
            Vec2f uv = directionToEquirect(ls.wi);
            int x = std::min(static_cast<int>(uv.x * W), W - 1);
            int y = std::min(static_cast<int>(uv.y * H), H - 1);
            if (x >= 8 && x < 24 && y >= 4 && y < 10) ++inside;
        }
    }
    EXPECT_GT(inside, N * N * 8 / 10);
}

TEST(EnvironmentLight, RotationKeepsPdfConsistent) {
    constexpr int W = 32, H = 16;
    Image img(W, H);
    for (int y = 4; y < 10; ++y)
        for (int x = 8; x < 14; ++x) img.setPixel(x, y, Color3f(50.0f));
    EnvironmentLight env(std::make_shared<const Image>(img), 1.3f, 2.0f);
    SurfaceInteraction si;
    for (int i = 0; i < 16; ++i) {
        LightSample ls = env.sampleLi(si, Vec2f((i + 0.5f) / 16.0f, 0.4f));
        ASSERT_GT(ls.pdf, 0.0f);
        EXPECT_NEAR(ls.pdf, env.pdfLi(ls.wi), std::max(1e-3f, ls.pdf * 0.15f));
        // Sampled directions land on the (rotated) bright patch, scaled by intensity.
        EXPECT_GT(ls.Li.r, 10.0f);
    }
}

TEST(EnvironmentLight, PolesDoNotWrap) {
    // Top row white, bottom row black: looking straight up must not blend in the nadir.
    Image img(8, 8);
    for (int x = 0; x < 8; ++x) img.setPixel(x, 0, Color3f(1.0f));
    EXPECT_NEAR(sampleEquirect(img, 0.3f, 0.0f).r, 1.0f, 1e-5f);
    EXPECT_NEAR(sampleEquirect(img, 0.3f, 1.0f).r, 0.0f, 1e-5f);
}

// MIS telafisi (Karlík 2019): telafili ışık örneklemesi + kosinüs BSDF örneklemesi, güç
// sezgiseli MIS ile yansız kalır (sönük yönleri BSDF stratejisi bulur) ve parlak pencereli bir
// gökte eşit örnekte daha az gürültü verir. Telafisiz karşılaştırma aynı tahminciyi tam
// dağılımla (sampleLiFull / pdfLiFull) çalıştırır.
TEST(EnvironmentLight, MisCompensationStaysUnbiasedAndReducesNoise) {
    constexpr int W = 64, H = 32;
    Image img(W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) img.setPixel(x, y, Color3f(0.6f)); // düzgün gök
    for (int y = 8; y < 13; ++y)
        for (int x = 20; x < 27; ++x) img.setPixel(x, y, Color3f(40.0f)); // parlak pencere
    EnvironmentLight env(std::make_shared<const Image>(img), 0.0f, 1.0f);
    ASSERT_TRUE(env.misCompensated());

    // Referans: yukarı bakan beyaz Lambert yüzeyin yansıttığı radyans = (1/π)∫L cosθ dω,
    // görüntü üzerinde kareleme ile.
    double ref = 0.0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const float v = (y + 0.5f) / H;
            const Vec3f d = equirectDirection((x + 0.5f) / W, v, 0.0f);
            ref += img.getPixel(x, y).g * std::max(0.0f, d.y) * (TWO_PI / W) * (PI / H) * std::sin(v * PI) * INV_PI;
        }

    // Integratördeki gibi tek örnekli MIS: bir ışık + bir BSDF örneği, güç sezgiseli.
    auto estimate = [&](bool compensated, uint64_t seed, int n, double& variance) {
        IndependentSampler rng(seed);
        auto lightPdf = [&](const Vec3f& wi) { return compensated ? env.pdfLi(wi) : env.pdfLiFull(wi); };
        SurfaceInteraction si;
        double sum = 0.0, sum2 = 0.0;
        for (int i = 0; i < n; ++i) {
            double e = 0.0;
            const LightSample ls = compensated ? env.sampleLi(si, rng.get2D()) : env.sampleLiFull(rng.get2D());
            if (ls.isValid() && ls.wi.y > 0.0f) {
                const float pl = ls.pdf, pb = ls.wi.y * INV_PI;
                e += ls.Li.g * ls.wi.y * INV_PI / pl * (pl * pl / (pl * pl + pb * pb));
            }
            const Vec3f l = cosineSampleHemisphere(rng.get2D());
            const Vec3f wi(l.x, l.z, l.y); // z-yukarı → y-yukarı
            const float pb = wi.y * INV_PI;
            if (pb > 0.0f) {
                const float pl = lightPdf(wi);
                e += env.eval(wi).g * wi.y * INV_PI / pb * (pb * pb / (pb * pb + pl * pl));
            }
            sum += e;
            sum2 += e * e;
        }
        const double mean = sum / n;
        variance = sum2 / n - mean * mean;
        return mean;
    };
    double varFull = 0.0, varComp = 0.0;
    const double full = estimate(false, 11, 400000, varFull);
    const double comp = estimate(true, 12, 400000, varComp);
    EXPECT_NEAR(full / ref, 1.0, 0.01);
    EXPECT_NEAR(comp / ref, 1.0, 0.01) << "telafili MIS yanlı";
    EXPECT_LT(varComp, varFull) << "telafi gürültüyü azaltmalı";
    std::printf("env MIS variance: full %.5f compensated %.5f (ref %.4f)\n", varFull, varComp, ref);
}

// Gerçek bir stüdyo HDRI'sı (softbox'lar + sönük oda): telafili NEE eşit örnekte daha temiz
// render verir, ortalama parlaklık (yansızlık) korunur.
TEST(EnvironmentLight, MisCompensationRendersStudioHdriCleaner) {
    auto hdr = loadImageHDR(std::string(PHOTON_ASSETS_DIR) + "/environments/studio_small_09_1k.hdr");
    if (!hdr) GTEST_SKIP() << "HDRI yok";
    auto map = std::make_shared<const Image>(std::move(*hdr));
    static const Lambertian floorMat(Color3f(0.7f));
    static const DisneyMaterial rough(Color3f(0.8f, 0.3f, 0.2f), 0.0f, 0.6f, 0.5f);
    static const DisneyMaterial metal(Color3f(0.9f), 1.0f, 0.3f, 0.5f);
    auto build = [&](Scene& scene) {
        const Vec3f up(0, 1, 0);
        std::vector<Vec3f> fp = {{-5, 0, -5}, {5, 0, -5}, {5, 0, 5}, {-5, 0, 5}};
        scene.addShape(std::make_shared<TriangleMesh>(fp, std::vector<Vec3f>{up, up, up, up}, std::vector<Vec2f>{},
                                                      std::vector<uint32_t>{0, 2, 1, 0, 3, 2}, &floorMat));
        scene.addShape(std::make_shared<Sphere>(Vec3f(-0.7f, 0.6f, 0.0f), 0.6f, &rough));
        scene.addShape(std::make_shared<Sphere>(Vec3f(0.7f, 0.6f, 0.0f), 0.6f, &metal));
        scene.setEnvironment(std::make_shared<EnvironmentLight>(map, 0.0f, 1.0f));
        scene.buildAccelerator();
    };
    PerspectiveCamera cam(Vec3f(0, 1.5f, 4.0f), Vec3f(0, 0.5f, 0), Vec3f(0, 1, 0), 45.0f, 1.0f);
    RenderSettings rs;
    rs.width = rs.height = 48;
    rs.maxBounces = 4;
    rs.filter = PixelFilter{PixelFilterType::Box, 1.0f};
    Renderer r;

    Scene comp;
    build(comp);
    ASSERT_TRUE(comp.environment()->misCompensated());
    rs.samplesPerPixel = 1024;
    const Image ref = r.render(comp, cam, rs);
    rs.samplesPerPixel = 16;
    const Image withComp = r.render(comp, cam, rs);

    EnvironmentLight::setMisCompensation(false);
    Scene full;
    build(full);
    EnvironmentLight::setMisCompensation(true);
    ASSERT_FALSE(full.environment()->misCompensated());
    const Image without = r.render(full, cam, rs);

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
    EXPECT_NEAR(mean(withComp) / mean(ref), 1.0, 0.03);
    EXPECT_NEAR(mean(without) / mean(ref), 1.0, 0.03);
    const double eComp = relMse(withComp, ref), eFull = relMse(without, ref);
    std::printf("studio HDRI @16spp relMSE: compensated %.5f, full %.5f\n", eComp, eFull);
    // Ölçüm (48², 16 spp; 1024 spp referans): stüdyo 0.059→0.052, brown_photostudio 0.037→0.025,
    // sunflowers_puresky (HDRI içinde güneş) 0.345→0.051 — parlak küçük kaynaklarda kazanç en büyük.
    EXPECT_LT(eComp, eFull);
}
