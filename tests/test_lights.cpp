// Işık testleri: küresel dikdörtgen örneklemesinin katı açısı, pdf tutarlılığı ve ışınımı
// (alan karelemesi referansına karşı), IES ayrıştırma ve simetri katlama, spot koni düşüşü,
// kameraya görünmez alan ışığı.

#include <gtest/gtest.h>
#include "engine/scene.h"
#include "integrators/path_tracer.h"
#include "lights/area_light.h"
#include "lights/environment_light.h"
#include "lights/ies.h"
#include "lights/spot_light.h"
#include "lights/sky_model.h"
#include "materials/disney.h"
#include "geometry/mesh.h"
#include "samplers/independent_sampler.h"

#include <cmath>
#include <memory>

using namespace photon;

namespace {

/// ∫ f(nokta) dA'yı ışığın üzerinde N×N orta nokta kuralıyla hesaplar.
template <typename F>
double quadrature(const AreaLight& l, int n, F f) {
    double sum = 0.0;
    const double dA = static_cast<double>(l.area()) / (n * n);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const Vec3f p = l.position() + l.u() * ((i + 0.5f) / n) + l.v() * ((j + 0.5f) / n);
            sum += f(p) * dA;
        }
    return sum;
}

} // namespace

TEST(SphericalRect, SolidAngleAndIrradianceMatchQuadrature) {
    // 2×1 softbox, alıcı noktalar yakından (geniş açı) uzağa kadar.
    AreaLight light(Vec3f(-1.0f, 2.0f, -0.5f), Vec3f(2, 0, 0), Vec3f(0, 0, 1), Color3f(5.0f));
    const Vec3f refs[] = {{0.0f, 1.6f, 0.0f}, {0.7f, 0.0f, 0.3f}, {-3.0f, 0.5f, 2.0f}, {0.2f, 3.0f, 0.1f}};
    IndependentSampler rng(17);
    for (const Vec3f& ref : refs) {
        const double omega = quadrature(light, 600, [&](const Vec3f& p) {
            const Vec3f d = p - ref;
            const float r2 = d.lengthSquared();
            return std::abs(d.dot(light.normal())) / (r2 * std::sqrt(r2));
        });
        EXPECT_NEAR(light.solidAngle(ref), omega, omega * 2e-3 + 1e-6);

        // Yatay bir alıcıda ışınım E = ∫ L cosθr dω.
        const Vec3f n(0, 1, 0);
        const double Eref = quadrature(light, 600, [&](const Vec3f& p) {
            const Vec3f d = p - ref;
            const float r2 = d.lengthSquared(), r = std::sqrt(r2);
            return 5.0 * std::max(0.0f, d.dot(n) / r) * std::abs(d.dot(light.normal())) / (r2 * r);
        });
        SurfaceInteraction si;
        si.point = ref;
        si.normal = n;
        double E = 0.0;
        const int N = 200000;
        for (int k = 0; k < N; ++k) {
            const LightSample ls = light.sampleLi(si, rng.get2D());
            if (!ls.isValid()) continue;
            // Örnek dikdörtgenin üstünde ve pdf'i pdfLi ile aynı.
            const Vec3f p = ref + ls.wi * ls.distance;
            if (k < 64) EXPECT_NEAR(light.pdfLi(ref, p), ls.pdf, ls.pdf * 1e-3f);
            E += ls.Li.r * std::max(0.0f, ls.wi.dot(n)) / ls.pdf;
        }
        E /= N;
        EXPECT_NEAR(E, Eref, std::max(Eref * 0.01, 1e-4)) << "ref " << ref.x << "," << ref.y << "," << ref.z;
    }
}

TEST(Ies, ParsesAndFoldsSymmetry) {
    // 3 dikey (0, 45, 90) × 2 yatay (0, 90) açı, dörtte bir simetri.
    const std::string text =
        "IESNA:LM-63-2002\n[TEST] birim\nTILT=NONE\n"
        "1 1000 1 3 2 1 1 0 0 0\n1 1 50\n"
        "0 45 90\n0 90\n"
        "200 100 0\n"   // φ = 0
        "100 50 0\n";   // φ = 90
    std::string err;
    auto p = parseIes(text, &err);
    ASSERT_TRUE(p) << err;
    EXPECT_FLOAT_EQ(p->peakCandela, 200.0f);
    EXPECT_NEAR(p->evaluate(0.0f, 0.0f), 1.0f, 1e-6f);
    EXPECT_NEAR(p->evaluate(22.5f, 0.0f), 0.75f, 1e-6f);
    EXPECT_NEAR(p->evaluate(0.0f, 90.0f), 0.5f, 1e-6f);
    EXPECT_NEAR(p->evaluate(0.0f, 45.0f), 0.75f, 1e-6f);
    // Simetri: 270° → 90°, 180° → 0°, 135° → 45°.
    EXPECT_NEAR(p->evaluate(0.0f, 270.0f), 0.5f, 1e-6f);
    EXPECT_NEAR(p->evaluate(0.0f, 180.0f), 1.0f, 1e-6f);
    EXPECT_NEAR(p->evaluate(0.0f, 135.0f), 0.75f, 1e-6f);
    // Tablonun dışı (yukarı) karanlık.
    EXPECT_EQ(p->evaluate(120.0f, 0.0f), 0.0f);
    EXPECT_FALSE(parseIes("bozuk", &err));
}

TEST(SpotLight, ConeFalloffAndInverseSquare) {
    SpotLight spot(Vec3f(0, 2, 0), Vec3f(0, -1, 0), Color3f(8.0f), 60.0f, 0.5f);
    EXPECT_NEAR(spot.falloff(Vec3f(0, -1, 0)), 1.0f, 1e-6f);
    // 30° yarı açının dışı karanlık, iç koni (15°) tam parlak.
    const auto dirAt = [](float deg) {
        const float a = deg * DEG_TO_RAD;
        return Vec3f(std::sin(a), -std::cos(a), 0.0f);
    };
    EXPECT_EQ(spot.falloff(dirAt(31.0f)), 0.0f);
    EXPECT_NEAR(spot.falloff(dirAt(14.0f)), 1.0f, 1e-6f);
    const float mid = spot.falloff(dirAt(22.5f));
    EXPECT_GT(mid, 0.1f);
    EXPECT_LT(mid, 0.9f);
    SurfaceInteraction si;
    si.point = Vec3f(0, 0, 0);
    si.normal = Vec3f(0, 1, 0);
    const LightSample ls = spot.sampleLi(si, Vec2f(0.5f));
    EXPECT_NEAR(ls.Li.r, 8.0f / 4.0f, 1e-5f);
}

TEST(AreaLight, CameraInvisibleQuadShowsBackground) {
    for (bool visible : {true, false}) {
        Scene scene;
        auto light = std::make_shared<AreaLight>(Vec3f(-1, -1, 0), Vec3f(2, 0, 0), Vec3f(0, 2, 0), Color3f(10.0f));
        light->setVisibleToCamera(visible);
        scene.addLight(light);
        scene.setEnvironment(std::make_shared<EnvironmentLight>(nullptr, 0.0f, 0.4f)); // 0.2 her yönde
        scene.buildAccelerator();
        PathTracer pt(4);
        IndependentSampler sampler(1);
        const Color3f L = pt.Li(Ray(Vec3f(0.1f, 0.2f, 5.0f), Vec3f(0, 0, -1)), scene, sampler);
        if (visible) EXPECT_NEAR(L.r, 10.0f, 1e-4f);
        else EXPECT_NEAR(L.r, 0.2f, 1e-4f);
    }
}

TEST(Sky, SolarPositionIstanbulSummerNoon) {
    // 21 Haziran, İstanbul: güneş öğlesi ~13:06 (UTC+3), yükseklik 90 − (41.01 − 23.44) ≈ 72.4°.
    float el = 0.0f, az = 0.0f;
    solarPosition(41.01f, 28.98f, 172, 13.1f, 3.0f, el, az);
    EXPECT_NEAR(el, 72.4f, 0.6f);
    EXPECT_NEAR(az, 180.0f, 4.0f);
    // Sabah doğuda, akşam batıda.
    solarPosition(41.01f, 28.98f, 172, 8.0f, 3.0f, el, az);
    EXPECT_GT(el, 10.0f);
    EXPECT_LT(az, 120.0f);
    solarPosition(41.01f, 28.98f, 172, 19.0f, 3.0f, el, az);
    EXPECT_GT(az, 240.0f);
}

TEST(Sky, NoonIsBlueSunsetIsRed) {
    SkyParams noon;
    noon.sunElevationDeg = 60.0f;
    noon.width = 128;
    const SkyResult a = buildPhysicalSky(noon);
    // Zenit (görüntünün en üst satırı) mavi; öğlen güneşi neredeyse beyaz ve parlak.
    const Color3f zen = a.image->getPixel(10, 0);
    EXPECT_GT(zen.b, zen.r);
    EXPECT_GT(a.sunRadiance.r, 0.0f);
    const float omega = TWO_PI * (1.0f - a.sunCosMax);
    EXPECT_GT(a.sunRadiance.g * omega, 0.6f * noon.sunStrength);
    EXPECT_LT(a.sunRadiance.g * omega, noon.sunStrength);

    SkyParams sunset = noon;
    sunset.sunElevationDeg = 2.0f;
    const SkyResult b = buildPhysicalSky(sunset);
    EXPECT_GT(b.sunRadiance.r, 2.0f * b.sunRadiance.b); // batan güneş kızıl
}

TEST(Sky, SunDiskSamplingMatchesPdfAndIrradiance) {
    SkyParams p;
    p.sunElevationDeg = 40.0f;
    p.width = 128;
    const SkyResult sky = buildPhysicalSky(p);
    EnvironmentLight env(sky.image, 0.0f, 1.0f);
    env.setSun(sky.sunDirection, sky.sunCosMax, sky.sunRadiance);
    ASSERT_TRUE(env.hasSun());

    SurfaceInteraction si;
    si.point = Vec3f(0.0f);
    si.normal = Vec3f(0, 1, 0);
    IndependentSampler rng(3);
    double E = 0.0;
    const int N = 400000;
    for (int i = 0; i < N; ++i) {
        const LightSample ls = env.sampleLi(si, rng.get2D());
        if (!ls.isValid()) continue;
        if (i < 200) EXPECT_NEAR(env.pdfLi(ls.wi), ls.pdf, ls.pdf * 1e-3f);
        E += ls.Li.g * std::max(0.0f, ls.wi.y) / ls.pdf;
    }
    E /= N;
    // Referans: güneş (analitik) + gök (görüntü üzerinde kareleme).
    const float omega = TWO_PI * (1.0f - sky.sunCosMax);
    double Eref = sky.sunRadiance.g * omega * sky.sunDirection.y;
    const int W = sky.image->width(), H = sky.image->height();
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const float v = (y + 0.5f) / H;
            const Vec3f d = equirectDirection((x + 0.5f) / W, v, 0.0f);
            const double dOmega = (TWO_PI / W) * (PI / H) * std::sin(v * PI);
            Eref += sky.image->getPixel(x, y).g * std::max(0.0f, d.y) * dOmega;
        }
    EXPECT_NEAR(E, Eref, Eref * 0.02);
}

TEST(ShadowCatcher, ShowsBackgroundDarkenedOnlyInShadow) {
    Scene scene;
    DisneyMaterial ground(Color3f(0.5f), 0.0f, 0.5f, 0.5f);
    auto quad = std::make_shared<TriangleMesh>(
        std::vector<Vec3f>{{-10, 0, -10}, {10, 0, -10}, {10, 0, 10}, {-10, 0, 10}}, std::vector<Vec3f>{},
        std::vector<Vec2f>{}, std::vector<uint32_t>{0, 2, 1, 0, 3, 2}, &ground);
    scene.setShadowCatcher(quad.get());
    scene.addShape(quad);
    // Işığı tamamen kesen yatay bir levha (x < −0.5 bölgesinin üstünde; kamera altından bakar).
    DisneyMaterial grey(Color3f(0.5f), 0.0f, 0.5f, 0.5f);
    scene.addShape(std::make_shared<TriangleMesh>(
        std::vector<Vec3f>{{-10, 1, -10}, {-0.5f, 1, -10}, {-0.5f, 1, 10}, {-10, 1, 10}}, std::vector<Vec3f>{},
        std::vector<Vec2f>{}, std::vector<uint32_t>{0, 2, 1, 0, 3, 2}, &grey));
    auto light = std::make_shared<AreaLight>(Vec3f(-3, 5, -1), Vec3f(6, 0, 0), Vec3f(0, 0, 2), Color3f(20.0f));
    scene.addLight(light);
    Background bg;
    bg.mode = Background::Mode::Color;
    bg.color = Color3f(0.8f, 0.7f, 0.6f);
    scene.setBackground(bg);
    scene.buildAccelerator();

    PathTracer pt(4);
    IndependentSampler sampler(2);
    auto shade = [&](float x) {
        Color3f sum(0.0f);
        float alpha = 0.0f;
        const int n = 400;
        for (int i = 0; i < n; ++i) {
            PathTracer::PrimaryHit ph;
            sum += pt.Li(Ray(Vec3f(x, 0.5f, 3), (Vec3f(x, 0, 0) - Vec3f(x, 0.5f, 3)).normalized()), scene, sampler, &ph);
            alpha += ph.catcherAlpha;
        }
        return std::make_pair(sum / static_cast<float>(n), alpha / n);
    };
    const auto lit = shade(3.0f);
    EXPECT_NEAR(lit.first.r, 0.8f, 1e-3f); // açıkta: arka planın kendisi
    EXPECT_NEAR(lit.second, 0.0f, 1e-3f);
    const auto dark = shade(-4.0f);
    EXPECT_NEAR(dark.first.r, 0.0f, 1e-3f); // tam gölgede: siyah
    EXPECT_NEAR(dark.second, 1.0f, 1e-3f);
}
