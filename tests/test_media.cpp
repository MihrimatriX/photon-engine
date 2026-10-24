// Hacim ve spektral testleri: Henyey–Greenstein örneklemesi, renkli camın Beer–Lambert
// soğurması, saçılan ortamda beyaz fırın (enerji korunumu), yüzey altı rastgele yürüyüşün
// albedo ters çevirmesi ve dispersiyonda dalga boyu ağırlıklarının beyaz ortalaması.

#include <gtest/gtest.h>
#include "core/color/spectral.h"
#include "core/sampling/phase.h"
#include "core/sampling/sampling.h"
#include "engine/scene.h"
#include "geometry/mesh.h"
#include "geometry/sphere.h"
#include "integrators/path_tracer.h"
#include "lights/environment_light.h"
#include "lights/area_light.h"
#include "materials/dielectric.h"
#include "materials/disney.h"
#include "samplers/independent_sampler.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace photon;

namespace {

/// Eksen hizalı kapalı kutu; üçgenler dışa bakan geometrik normalle örülür.
std::shared_ptr<TriangleMesh> makeBox(const Vec3f& lo, const Vec3f& hi, const Material* mat) {
    std::vector<Vec3f> p;
    std::vector<uint32_t> idx;
    const Vec3f c = (lo + hi) * 0.5f;
    auto quad = [&](Vec3f a, Vec3f b, Vec3f cc, Vec3f d) {
        const uint32_t base = static_cast<uint32_t>(p.size());
        Vec3f n = (b - a).cross(cc - a);
        const bool outward = n.dot(((a + cc) * 0.5f) - c) > 0.0f;
        p.insert(p.end(), {a, b, cc, d});
        if (outward) idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        else idx.insert(idx.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
    };
    const Vec3f v[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                        {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
    quad(v[0], v[1], v[2], v[3]);
    quad(v[4], v[5], v[6], v[7]);
    quad(v[0], v[1], v[5], v[4]);
    quad(v[3], v[2], v[6], v[7]);
    quad(v[0], v[3], v[7], v[4]);
    quad(v[1], v[2], v[6], v[5]);
    return std::make_shared<TriangleMesh>(p, std::vector<Vec3f>{}, std::vector<Vec2f>{}, idx, mat);
}

/// Her yönden radyansı 1 olan ortam (beyaz fırın).
std::shared_ptr<EnvironmentLight> whiteFurnace() {
    return std::make_shared<EnvironmentLight>(nullptr, 0.0f, 2.0f); // görüntüsüz: 0.5 · yoğunluk
}

Color3f averageLi(const PathTracer& pt, const Scene& scene, const Ray& ray, int n, uint64_t seed = 3) {
    IndependentSampler sampler(seed);
    Color3f sum(0.0f);
    for (int i = 0; i < n; ++i) sum += pt.Li(ray, scene, sampler);
    return sum / static_cast<float>(n);
}

} // namespace

TEST(Phase, HenyeyGreensteinMeanCosineIsG) {
    IndependentSampler rng(11);
    const Vec3f d = Vec3f(0.3f, -0.5f, 0.8f).normalized();
    for (float g : {0.0f, 0.6f, -0.4f}) {
        double sum = 0.0;
        const int n = 200000;
        for (int i = 0; i < n; ++i) sum += sampleHenyeyGreenstein(d, g, rng.get2D()).dot(d);
        EXPECT_NEAR(sum / n, g, 0.01) << "g=" << g;
    }
}

TEST(Phase, HenyeyGreensteinIsNormalized) {
    for (float g : {0.0f, 0.8f, -0.5f}) {
        // ∫ p dω = 2π ∫_{-1}^{1} p(μ) dμ
        double sum = 0.0;
        const int n = 20000;
        for (int i = 0; i < n; ++i) {
            const float mu = -1.0f + 2.0f * (static_cast<float>(i) + 0.5f) / n;
            sum += henyeyGreenstein(mu, g) * (2.0 / n);
        }
        EXPECT_NEAR(sum * 2.0 * PI, 1.0, 1e-3) << "g=" << g;
    }
}

// İndisi havayla eşit (n = 1) cam: Fresnel 0, kırılma yok; yalnız soğurma kalır.
// Kalınlık 2, "1 birimde (0.5, 0.25, 1) renk" → T = (0.25, 0.0625, 1).
TEST(Media, GlassAbsorptionFollowsBeerLambert) {
    Scene scene;
    auto glass = std::make_shared<Dielectric>(1.0f, Color3f(1.0f));
    glass->setAbsorption(Color3f(0.5f, 0.25f, 1.0f), 1.0f);
    scene.addShape(makeBox(Vec3f(-5, -5, -1), Vec3f(5, 5, 1), glass.get()));
    scene.setEnvironment(whiteFurnace());
    scene.buildAccelerator();

    PathTracer pt(16, 64);
    Ray ray(Vec3f(0.1f, 0.2f, 10.0f), Vec3f(0, 0, -1));
    const Color3f L = averageLi(pt, scene, ray, 16);
    EXPECT_NEAR(L.r, 0.25f, 1e-3f);
    EXPECT_NEAR(L.g, 0.0625f, 1e-3f);
    EXPECT_NEAR(L.b, 1.0f, 1e-3f);
}

// Soğurmasız, yalnız saçan ortam (albedo 1) beyaz fırında enerjiyi korur: Li = 1.
// Renkli σt (kanal başına farklı yoğunluk) spektral MIS yolunu da sınar.
TEST(Media, ScatteringMediumConservesEnergyInFurnace) {
    Scene scene;
    auto glass = std::make_shared<Dielectric>(1.0f, Color3f(1.0f));
    glass->setScattering(1.5f, Color3f(1.0f), 0.3f);
    scene.addShape(makeBox(Vec3f(-1, -1, -1), Vec3f(1, 1, 1), glass.get()));
    scene.setEnvironment(whiteFurnace());
    scene.buildAccelerator();

    PathTracer pt(64, 64);
    Ray ray(Vec3f(0.2f, 0.1f, 10.0f), Vec3f(0, 0, -1));
    const Color3f L = averageLi(pt, scene, ray, 20000);
    EXPECT_NEAR(L.r, 1.0f, 0.03f);
    EXPECT_NEAR(L.g, 1.0f, 0.03f);
    EXPECT_NEAR(L.b, 1.0f, 0.03f);
}

// Kırıcı (n = 1.5), soğurmasız cam küre beyaz fırında beyaz kalır; dispersiyonla
// (Abbe 20) her yol tek renkli olsa da ortalama yine beyazdır.
TEST(Media, DispersiveGlassStaysWhiteInFurnace) {
    Scene scene;
    auto glass = std::make_shared<Dielectric>(1.5f, Color3f(1.0f));
    glass->setAbbe(20.0f);
    scene.addShape(std::make_shared<Sphere>(Vec3f(0.0f), 1.0f, glass.get()));
    scene.setEnvironment(whiteFurnace());
    scene.buildAccelerator();

    PathTracer pt(64, 64);
    Ray ray(Vec3f(0.35f, 0.2f, 10.0f), Vec3f(0, 0, -1));
    const Color3f L = averageLi(pt, scene, ray, 40000);
    EXPECT_NEAR(L.r, 1.0f, 0.04f);
    EXPECT_NEAR(L.g, 1.0f, 0.04f);
    EXPECT_NEAR(L.b, 1.0f, 0.04f);
}

TEST(Spectral, WavelengthWeightsAverageToWhite) {
    Color3f sum(0.0f);
    const int n = 100000;
    for (int i = 0; i < n; ++i) {
        Color3f w;
        const float l = sampleWavelength((static_cast<float>(i) + 0.5f) / n, w);
        EXPECT_GE(l, kLambdaMin);
        EXPECT_LE(l, kLambdaMax);
        EXPECT_TRUE(w.isValid());
        sum += w;
    }
    sum /= static_cast<float>(n);
    EXPECT_NEAR(sum.r, 1.0f, 0.01f);
    EXPECT_NEAR(sum.g, 1.0f, 0.01f);
    EXPECT_NEAR(sum.b, 1.0f, 0.01f);
    // Kırmızı dalga boyu kırmızı, mavi dalga boyu mavi ağırlık taşır.
    const Color3f red = wavelengthToRGB(650.0f), blue = wavelengthToRGB(450.0f);
    EXPECT_GT(red.r, red.b);
    EXPECT_GT(blue.b, blue.r);
}

TEST(Spectral, CauchyMatchesAbbeDefinition) {
    const float nd = 1.5168f, V = 64.17f; // N-BK7
    EXPECT_NEAR(cauchyIor(nd, V, 587.56f), nd, 1e-4f);
    const float nF = cauchyIor(nd, V, 486.13f), nC = cauchyIor(nd, V, 656.27f);
    EXPECT_GT(nF, nC);
    EXPECT_NEAR((nd - 1.0f) / (nF - nC), V, 0.05f);
    EXPECT_FLOAT_EQ(cauchyIor(nd, 0.0f, 450.0f), nd);
}

// Yarı sonsuz levha (büyük kutu): kosinüs ağırlıklı (integratördeki gibi) giren
// yürüyüşlerin geri çıkan enerjisi Chiang 2016 ters çevirmesinin hedeflediği renge (A)
// yakın olmalı; çıkış noktaları da girişin yarıçap ölçeğinde bir komşuluğunda kalmalı.
// (Dik girişte yansıma daha düşüktür; Hapke: A = 0.5 için ~0.44. Uydurma difüz giriş içindir.)
TEST(Subsurface, RandomWalkReproducesTargetAlbedo) {
    Scene scene;
    DisneyMaterial skin(Color3f(0.8f, 0.5f, 0.2f), 0.0f, 0.5f, 0.5f);
    skin.setSubsurface(1.0f);
    skin.setSubsurfaceRadius(Color3f(1.0f));
    skin.setSubsurfaceScale(0.02f);
    scene.addShape(makeBox(Vec3f(-50, -100, -50), Vec3f(50, 0, 50), &skin));
    scene.buildAccelerator();

    SurfaceInteraction entry;
    entry.point = Vec3f(0, 0, 0);
    entry.normal = entry.ng = Vec3f(0, 1, 0);
    entry.material = &skin;
    SubsurfaceCoeffs coeffs;
    ASSERT_TRUE(skin.subsurfaceCoeffs(entry, coeffs));

    IndependentSampler rng(5);
    Color3f sum(0.0f);
    double dist = 0.0;
    int exits = 0;
    const int n = 20000;
    for (int i = 0; i < n; ++i) {
        Color3f T(1.0f);
        SurfaceInteraction exitSi;
        Vec3f dir = cosineSampleHemisphere(rng.get2D());
        dir = Vec3f(dir.x, -dir.z, dir.y);
        if (PathTracer::randomWalkSubsurface(scene, entry, dir, coeffs, rng, T, exitSi)) {
            sum += T;
            dist += (exitSi.point - entry.point).length();
            ++exits;
            EXPECT_GT(exitSi.point.y, -1e-3f); // üst yüzden çıktı
        }
    }
    sum /= static_cast<float>(n);
    EXPECT_NEAR(sum.r, 0.8f, 0.05f);
    EXPECT_NEAR(sum.g, 0.5f, 0.05f);
    EXPECT_NEAR(sum.b, 0.2f, 0.03f);
    ASSERT_GT(exits, 0);
    EXPECT_LT(dist / exits, 0.2); // ~ birkaç ortalama serbest yol (0.02)
}

// Yüzey altı lobu yalnız sampleFull'da görünür; eval/pdf onu içermez ve eski sample()
// o lobu seçtiğinde başarısız döner (rastgele yürüyüşü yalnız integratör yapabilir).
TEST(Subsurface, LobeIsSampledOnlyThroughSampleFull) {
    DisneyMaterial m(Color3f(0.9f), 0.0f, 0.5f, 0.5f);
    m.setSubsurface(1.0f);
    SurfaceInteraction si;
    si.point = Vec3f(0.0f);
    si.normal = si.ng = Vec3f(0, 0, 1);
    si.tangent = Vec3f(1, 0, 0);
    const Vec3f wo = Vec3f(0.2f, 0.1f, 1.0f).normalized();
    IndependentSampler rng(9);
    int sss = 0;
    const int n = 4000;
    for (int i = 0; i < n; ++i) {
        BSDFSample bs;
        if (!m.sampleFull(wo, si, rng.get1D(), rng.get2D(), bs)) continue;
        if (bs.subsurface) {
            ++sss;
            EXPECT_LT(bs.wi.dot(si.normal), 0.0f); // içeri girer
            EXPECT_TRUE(bs.weight.isValid());
        }
    }
    EXPECT_GT(sss, n / 10);
}

// İnce duvarlı cam (zar) kayıpsızdır: R + T = 1. Beyaz fırında Li = 1, film olsa da.
TEST(ThinWalled, ConservesEnergyInFurnaceWithAndWithoutFilm) {
    for (float film : {0.0f, 450.0f}) {
        Scene scene;
        auto pane = std::make_shared<Dielectric>(1.33f, Color3f(1.0f));
        pane->setThinWalled(true);
        pane->setThinFilm(film);
        scene.retainMaterial(pane);
        scene.addShape(std::make_shared<Sphere>(Vec3f(0.0f), 1.0f, pane.get()));
        scene.setEnvironment(whiteFurnace());
        scene.buildAccelerator();
        PathTracer pt(16, 64);
        const Color3f L = averageLi(pt, scene, Ray(Vec3f(0.3f, 0.1f, 10.0f), Vec3f(0, 0, -1)), 4000);
        EXPECT_NEAR(L.r, 1.0f, 0.02f) << "film " << film;
        EXPECT_NEAR(L.b, 1.0f, 0.02f) << "film " << film;
    }
}

// İndisi 1 olan ince duvar tamamen saydamdır: ışık ile difüz zemin arasına konunca sonuç
// cam yokkenkiyle aynı olmalı. Gölge ışını camdan geçerken BSDF ışını da geçer; MIS durumu
// korunmasaydı ışık iki kez sayılır ve zemin parlak çıkardı.
TEST(ThinWalled, TransparentPaneDoesNotDoubleCountLight) {
    auto shade = [](bool withPane) {
        Scene scene;
        DisneyMaterial floorMat(Color3f(0.7f), 0.0f, 0.6f, 0.5f);
        scene.addShape(makeBox(Vec3f(-5, -1, -5), Vec3f(5, 0, 5), &floorMat));
        auto pane = std::make_shared<Dielectric>(1.0f, Color3f(1.0f));
        pane->setThinWalled(true);
        if (withPane) {
            scene.retainMaterial(pane);
            scene.addShape(std::make_shared<TriangleMesh>(
                std::vector<Vec3f>{{-3, 1, -3}, {3, 1, -3}, {3, 1, 3}, {-3, 1, 3}}, std::vector<Vec3f>{},
                std::vector<Vec2f>{}, std::vector<uint32_t>{0, 2, 1, 0, 3, 2}, pane.get()));
        }
        scene.addLight(std::make_shared<AreaLight>(Vec3f(-0.5f, 2.5f, -0.5f), Vec3f(1, 0, 0), Vec3f(0, 0, 1), Color3f(10.0f)));
        scene.buildAccelerator();
        PathTracer pt(3, 64);
        return averageLi(pt, scene, Ray(Vec3f(0.2f, 0.5f, 0.3f), Vec3f(0, -1, 0)), 20000, 11);
    };
    const Color3f open = shade(false), pane = shade(true);
    EXPECT_NEAR(pane.g, open.g, open.g * 0.03f);
}

// Yuvarlatılmış kenar: küpün yüz ortasında normal değişmez, kenara R'den yakın noktada
// komşu yüze doğru eğilir.
TEST(RoundEdges, NormalBendsOnlyNearEdges) {
    Scene scene;
    DisneyMaterial m(Color3f(0.5f), 0.0f, 0.5f, 0.5f);
    m.setRoundEdgeRadius(0.1f);
    auto box = makeBox(Vec3f(-1, -1, -1), Vec3f(1, 1, 1), &m);
    scene.addShape(box);
    scene.buildAccelerator();
    // Yuvarlatma integratörün içinde yapılır; test kancası aynı hesabı üst yüzdeki
    // birincil isabette tekrarlar (64 örneğin ortalaması).
    auto normalAt = [&](float x) {
        IndependentSampler rng(2);
        Vec3f sum(0.0f);
        for (int i = 0; i < 64; ++i) {
            Ray r(Vec3f(x, 3.0f, 0.0f), Vec3f(0, -1, 0));
            SurfaceInteraction si;
            if (!scene.intersect(r, si)) continue;
            sum += PathTracer::roundedEdgeNormalForTest(scene, si, 0.1f, rng);
        }
        return sum.normalized();
    };
    EXPECT_GT(normalAt(0.0f).y, 0.999f);   // yüz ortası
    const Vec3f edge = normalAt(0.97f);     // kenara 0.03
    EXPECT_LT(edge.y, 0.97f);
    EXPECT_GT(edge.x, 0.15f);               // +X yüzüne doğru eğildi
}

// Regresyon: kameradan doğrudan görülen ince duvarlı camın arkasındaki düz renk fon camın
// tonunu almıyordu (L = fon rengi atanıyordu), şeffaf fonda cam opak siyah çıkıyordu.
TEST(ThinWalled, TintAppliesOverColorAndTransparentBackground) {
    for (auto mode : {Background::Mode::Color, Background::Mode::Transparent}) {
        Scene scene;
        auto pane = std::make_shared<Dielectric>(1.5f, Color3f(0.2f, 1.0f, 0.2f));
        pane->setThinWalled(true);
        scene.retainMaterial(pane);
        scene.addShape(std::make_shared<TriangleMesh>(
            std::vector<Vec3f>{{-2, -2, 0}, {2, -2, 0}, {2, 2, 0}, {-2, 2, 0}}, std::vector<Vec3f>{},
            std::vector<Vec2f>{}, std::vector<uint32_t>{0, 1, 2, 0, 2, 3}, pane.get()));
        scene.setEnvironment(whiteFurnace());
        Background bg;
        bg.mode = mode;
        bg.color = Color3f(1.0f);
        scene.setBackground(bg);
        scene.buildAccelerator();
        PathTracer pt(8);
        IndependentSampler sampler(6);
        Color3f sum(0.0f);
        float cov = 0.0f;
        const int n = 4000;
        for (int i = 0; i < n; ++i) {
            PathTracer::PrimaryHit ph;
            sum += pt.Li(Ray(Vec3f(0.1f, 0.1f, 5.0f), Vec3f(0, 0, -1)), scene, sampler, &ph);
            cov += ph.coverage >= 0.0f ? ph.coverage : 1.0f;
        }
        sum /= static_cast<float>(n);
        if (mode == Background::Mode::Color) {
            // Geçen: ton × (1 − R_levha) ≈ (0.2, 0.92, 0.2); yansıyan: beyaz fırın × R ≈ 0.08.
            EXPECT_LT(sum.r, 0.45f);
            EXPECT_GT(sum.g, 0.9f);
        } else {
            EXPECT_LT(cov / n, 0.6f); // kapsama artık 1 (opak) değil
        }
    }
}

// Regresyon: ortalama (yumuşak) normalli cam küpün içindeki saçan ortam, silüette geometrik
// normale düşen çerçeve yüzünden her sekmede throughput'u şişiriyordu (fırında ~1e24).
TEST(Media, SmoothShadedGlassWithScatteringStaysInFurnace) {
    // 8 köşe paylaşımlı küp: köşe normalleri merkezden dışa (yumuşak gölgeleme).
    std::vector<Vec3f> p, n;
    for (int i = 0; i < 8; ++i) {
        const Vec3f v((i & 1) ? 1.f : -1.f, (i & 2) ? 1.f : -1.f, (i & 4) ? 1.f : -1.f);
        p.push_back(v);
        n.push_back(v.normalized());
    }
    const std::vector<uint32_t> idx = {0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4,
                                       2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5};
    Scene scene;
    auto glass = std::make_shared<Dielectric>(1.5f, Color3f(1.0f));
    glass->setScattering(3.0f, Color3f(1.0f), 0.0f);
    scene.addShape(std::make_shared<TriangleMesh>(p, n, std::vector<Vec2f>{}, idx, glass.get()));
    scene.setEnvironment(whiteFurnace());
    scene.buildAccelerator();
    PathTracer pt(64, 64);
    const Color3f L = averageLi(pt, scene, Ray(Vec3f(0.3f, 0.2f, 10.0f), Vec3f(0, 0, -1)), 20000);
    EXPECT_NEAR(L.g, 1.0f, 0.1f);
}

TEST(Spectral, TinyAbbeNeverGivesIorBelowOne) {
    Dielectric g(1.5f, Color3f(1.0f));
    g.setAbbe(0.5f);
    EXPECT_GE(g.abbe(), 10.0f);
    SurfaceInteraction si;
    for (float l : {380.0f, 550.0f, 730.0f}) {
        si.wavelength = l;
        EXPECT_GE(g.iorAt(si), 1.0f);
    }
}
