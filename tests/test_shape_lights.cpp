// test_shape_lights.cpp — Disk, küre ve silindir (tüp) ışıkları ile tek yüzlü alan ışıkları.
// Üç katman sınanır:
//   1. Analitik şekiller (Disk, Cylinder): doğru yere çarpma, yüz yönü, boy/elips sınırı.
//   2. Işık örneklemesi: sampleLi'nin pdf'i, aynı noktaya çarpan ışının pdfLiHit'iyle aynı mı
//      (MIS'in doğru olması buna bağlı) ve tahminci analitik ışınımı veriyor mu?
//   3. Path tracer: zemindeki parlaklık NEE + BSDF isabetinin MIS ile toplamıdır; pdf'ler
//      tutarsızsa ağırlıkların toplamı 1 olmaz ve sonuç analitik değerden sapar.
#include "gtest/gtest.h"
#include "core/math/constants.h"
#include "engine/scene.h"
#include "geometry/cylinder.h"
#include "geometry/disk.h"
#include "geometry/triangle.h"
#include "integrators/path_tracer.h"
#include "lights/area_light.h"
#include "lights/light_sampler.h"
#include "lights/shape_lights.h"
#include "materials/lambertian.h"
#include "samplers/independent_sampler.h"
#include <cmath>
#include <memory>
#include <vector>

using namespace photon;

namespace {

SurfaceInteraction receiver(const Vec3f& p, const Vec3f& n) {
    SurfaceInteraction si;
    si.point = p;
    si.normal = n;
    si.ng = n;
    return si;
}

/// sampleLi ile E = ∫ L cos θ dω tahmini (gölgesiz).
float estimateIrradiance(const Light& light, const SurfaceInteraction& si, int n, uint64_t seed) {
    IndependentSampler rng(seed);
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        const LightSample ls = light.sampleLi(si, rng.get2D());
        if (!ls.isValid()) continue;
        sum += static_cast<double>(ls.Li.r) * std::max(0.0f, ls.wi.dot(si.normal)) / ls.pdf;
    }
    return static_cast<float>(sum / n);
}

/// Tüpün yan yüzeyi üzerinde ince kareleme: E = ∫ L max(0, n·w) max(0, −w·n_l) / d² dA.
/// Kapalı silindir dışbükey olduğundan dışarıdaki alıcıya bakan her nokta görünür.
float tubeIrradianceQuadrature(const CylinderLight& tube, const Vec3f& p, const Vec3f& n) {
    const Vec3f a = tube.axis();
    const Vec3f helper = std::abs(a.y) > 0.9f ? Vec3f(1, 0, 0) : Vec3f(0, 1, 0);
    const Vec3f b = helper.cross(a).normalized(), c = a.cross(b);
    const int nPhi = 1440, nZ = 480;
    const float h = tube.halfLength(), r = tube.radius();
    const float dA = (TWO_PI / nPhi) * r * (2.0f * h / nZ);
    double sum = 0.0;
    for (int i = 0; i < nPhi; ++i) {
        const float phi = TWO_PI * (static_cast<float>(i) + 0.5f) / nPhi;
        const Vec3f nl = b * std::cos(phi) + c * std::sin(phi);
        for (int j = 0; j < nZ; ++j) {
            const float z = -h + 2.0f * h * (static_cast<float>(j) + 0.5f) / nZ;
            const Vec3f x = tube.center() + a * z + nl * r;
            const Vec3f d = x - p;
            const float d2 = d.lengthSquared();
            const Vec3f w = d / std::sqrt(d2);
            const float cr = n.dot(w), cl = -w.dot(nl);
            if (cr > 0.0f && cl > 0.0f) sum += static_cast<double>(cr) * cl / d2;
        }
    }
    return static_cast<float>(sum * dA * tube.radiance().r);
}

void addFloor(Scene& scene, float half, const Material* mat) {
    const Vec3f n(0, 1, 0);
    const Vec3f p0(-half, 0, -half), p1(half, 0, -half), p2(half, 0, half), p3(-half, 0, half);
    const Vec2f uv(0, 0);
    // Normal +Y olacak sargı: (p3 − p0) × (p1 − p0) yukarı bakar.
    scene.addShape(std::make_shared<Triangle>(p0, p3, p2, n, n, n, uv, uv, uv, mat));
    scene.addShape(std::make_shared<Triangle>(p0, p2, p1, n, n, n, uv, uv, uv, mat));
}

/// Zeminin orijin noktasından yukarı bakan kameraya dönen ortalama parlaklık (tek saçılma).
float floorRadiance(const std::shared_ptr<Light>& light, int n, uint64_t seed) {
    auto floorMat = std::make_shared<Lambertian>(Color3f(0.5f));
    Scene scene;
    addFloor(scene, 50.0f, floorMat.get());
    scene.addLight(light);
    scene.buildAccelerator();
    PathTracer tracer(1, 100);
    IndependentSampler rng(seed);
    double sum = 0.0;
    // Işığın gölgesi düşmesin diye kamera ışını yandan, eğik gelir.
    const Ray ray(Vec3f(0.5f, 0.5f, 0.0f), Vec3f(-1.0f, -1.0f, 0.0f).normalized());
    for (int i = 0; i < n; ++i) sum += tracer.Li(ray, scene, rng).r;
    return static_cast<float>(sum / n);
}

} // namespace

// ── Şekiller ──────────────────────────────────────────────────────────────

TEST(ShapeLights, DiskHitsInsideEllipseOnly) {
    // ex × ey = (2,0,0) × (0,0,0.5) = (0,−1,0): normal aşağı.
    Disk disk(Vec3f(0, 1, 0), Vec3f(2, 0, 0), Vec3f(0, 0, 0.5f), nullptr);
    SurfaceInteraction isect;
    Ray up(Vec3f(1.9f, 0, 0), Vec3f(0, 1, 0));
    ASSERT_TRUE(disk.intersect(up, isect));
    EXPECT_NEAR(isect.t, 1.0f, 1e-5f);
    EXPECT_NEAR(isect.ng.y, -1.0f, 1e-6f);
    EXPECT_TRUE(isect.frontFace); // normalin baktığı taraftan geldi

    Ray down(Vec3f(0, 2, 0), Vec3f(0, -1, 0));
    ASSERT_TRUE(disk.intersect(down, isect));
    EXPECT_FALSE(isect.frontFace);

    Ray outsideZ(Vec3f(0, 0, 0.6f), Vec3f(0, 1, 0));
    EXPECT_FALSE(disk.intersect(outsideZ, isect));
    Ray corner(Vec3f(1.5f, 0, 0.4f), Vec3f(0, 1, 0)); // 0.75² + 0.8² > 1
    EXPECT_FALSE(disk.intersect(corner, isect));
    Ray parallel(Vec3f(-5, 1, 0), Vec3f(1, 0, 0));
    EXPECT_FALSE(disk.intersect(parallel, isect));

    const AABB b = disk.bounds();
    EXPECT_NEAR(b.pMax.x, 2.0f, 1e-5f);
    EXPECT_NEAR(b.pMax.z, 0.5f, 1e-5f);
}

TEST(ShapeLights, CylinderHitsSideWithinLength) {
    Cylinder tube(Vec3f(0, 0, 0), Vec3f(0, 0, 2), 0.5f, nullptr);
    SurfaceInteraction isect;
    Ray side(Vec3f(3, 0, 1), Vec3f(-1, 0, 0));
    ASSERT_TRUE(tube.intersect(side, isect));
    EXPECT_NEAR(isect.t, 2.5f, 1e-5f);
    EXPECT_NEAR(isect.point.x, 0.5f, 1e-5f);
    EXPECT_NEAR(isect.ng.x, 1.0f, 1e-5f);
    EXPECT_TRUE(isect.frontFace);

    Ray past(Vec3f(3, 0, 2.5f), Vec3f(-1, 0, 0));
    EXPECT_FALSE(tube.intersect(past, isect));
    Ray alongAxis(Vec3f(0, 0, -5), Vec3f(0, 0, 1));
    EXPECT_FALSE(tube.intersect(alongAxis, isect)); // uçlar açık: kapak yok

    Ray inside(Vec3f(0, 0, 0), Vec3f(1, 0, 0));
    ASSERT_TRUE(tube.intersect(inside, isect));
    EXPECT_NEAR(isect.t, 0.5f, 1e-5f);
    EXPECT_FALSE(isect.frontFace); // iç yüz

    // Açık uçtan eğik giren ışın iç duvara çarpar (x = 0.5 düzlemi z = 0'da).
    Ray throughEnd(Vec3f(0, 0, 5), Vec3f(0.1f, 0, -1).normalized());
    ASSERT_TRUE(tube.intersect(throughEnd, isect));
    EXPECT_NEAR(isect.point.z, 0.0f, 1e-4f);
    EXPECT_FALSE(isect.frontFace);

    const AABB b = tube.bounds();
    EXPECT_NEAR(b.pMax.z, 2.0f, 1e-5f);
    EXPECT_NEAR(b.pMax.x, 0.5f, 1e-5f);
}

// ── Örnekleme ve pdf tutarlılığı ──────────────────────────────────────────

TEST(ShapeLights, SampledPdfMatchesHitPdf) {
    std::vector<std::shared_ptr<Light>> lights = {
        std::make_shared<DiskLight>(Vec3f(0, 2, 0), Vec3f(0.8f, 0, 0), Vec3f(0, 0.2f, 0.4f), Color3f(3.0f), false),
        std::make_shared<DiskLight>(Vec3f(0, 2, 0), Vec3f(0.5f, 0, 0), Vec3f(0, 0, 0.5f), Color3f(3.0f), true),
        std::make_shared<SphereLight>(Vec3f(0.3f, 2, -0.2f), 0.4f, Color3f(5.0f)),
        std::make_shared<SphereLight>(Vec3f(0, 40, 0), 0.05f, Color3f(5.0f)), // küçük açı (Taylor) dalı
        std::make_shared<CylinderLight>(Vec3f(0, 2, 0), Vec3f(0.9f, 0.1f, 0.3f), 0.15f, Color3f(2.0f)),
    };
    const Vec3f refs[] = {Vec3f(0, 0, 0), Vec3f(1.5f, 0.5f, 1.0f), Vec3f(-2, 1, -1), Vec3f(0.2f, 3.5f, 0.1f)};
    for (size_t li = 0; li < lights.size(); ++li) {
        Scene scene;
        scene.addLight(lights[li]);
        scene.buildAccelerator();
        IndependentSampler rng(17 + li);
        int valid = 0;
        for (const Vec3f& p : refs) {
            const SurfaceInteraction si = receiver(p, Vec3f(0, 1, 0));
            for (int i = 0; i < 64; ++i) {
                const LightSample ls = lights[li]->sampleLi(si, rng.get2D());
                if (!ls.isValid()) continue;
                ++valid;
                Ray ray(p, ls.wi);
                SurfaceInteraction hit;
                ASSERT_TRUE(scene.intersect(ray, hit)) << "ışık " << li;
                EXPECT_NEAR(hit.t, ls.distance, 2e-3f * ls.distance) << "ışık " << li;
                // Örnek ışıyan yüzde: tek yüzlüde ön/dış yüz, iki yüzlüde herhangi biri.
                EXPECT_FALSE(hit.material->emitted(hit).isBlack()) << "ışık " << li;
                // Teğete yakın (sıyırarak geçen) ışında kesişim noktası kötü koşulludur (kök
                // çakışır) ve 1/cos θl pdf'i o hatayı büyütür; orada pdf karşılaştırılmaz.
                if (std::abs(ls.wi.dot(hit.ng)) < 0.02f) continue;
                EXPECT_NEAR(lights[li]->pdfLiHit(p, hit), ls.pdf, 2e-3f * ls.pdf) << "ışık " << li;
            }
        }
        EXPECT_GT(valid, 32) << "ışık " << li;
    }
}

TEST(ShapeLights, SphereMatchesAnalyticIrradiance) {
    // Kürenin altında, merkeze bakan alıcı: E = π L sin²θmax = π L (r/d)².
    SphereLight sphere(Vec3f(0, 3, 0), 0.5f, Color3f(2.0f));
    const SurfaceInteraction si = receiver(Vec3f(0, 0, 0), Vec3f(0, 1, 0));
    const float expected = PI * 2.0f * (0.25f / 9.0f);
    EXPECT_NEAR(estimateIrradiance(sphere, si, 20000, 3), expected, 0.01f * expected);
    // Uzaktan bakışta nokta ışık: I = L π r² → E ≈ I / d².
    SphereLight far(Vec3f(0, 30, 0), 0.01f, Color3f(2.0f));
    const float farE = 2.0f * PI * 1e-4f / 900.0f;
    EXPECT_NEAR(estimateIrradiance(far, si, 1000, 4), farE, 1e-3f * farE);
    // İçerideki alıcı ışık almaz (yalnız iç yüzü görür).
    const SurfaceInteraction inside = receiver(Vec3f(0, 3.1f, 0), Vec3f(0, 1, 0));
    EXPECT_FALSE(sphere.sampleLi(inside, Vec2f(0.3f, 0.6f)).isValid());
    EXPECT_FLOAT_EQ(sphere.importance(inside.point, inside.normal), 0.0f);
    EXPECT_NEAR(sphere.power().r, 2.0f * PI * 4.0f * PI * 0.25f, 1e-3f);
}

TEST(ShapeLights, DiskMatchesAnalyticIrradiance) {
    // Eksen üzerinde h uzaklıkta, diske bakan alıcı: E = π L r² / (h² + r²).
    DiskLight disk(Vec3f(0, 2, 0), Vec3f(0.8f, 0, 0), Vec3f(0, 0, 0.8f), Color3f(1.5f), false);
    ASSERT_LT(disk.normal().y, -0.99f); // aşağı ışır
    const SurfaceInteraction si = receiver(Vec3f(0, 0, 0), Vec3f(0, 1, 0));
    const float expected = PI * 1.5f * 0.64f / (4.0f + 0.64f);
    EXPECT_NEAR(estimateIrradiance(disk, si, 40000, 5), expected, 0.01f * expected);
    EXPECT_NEAR(disk.area(), PI * 0.64f, 1e-5f);
}

TEST(ShapeLights, TubeMatchesQuadrature) {
    // Eğik duran tüp; alıcılar yandan, alttan ve tüpün uzantısına yakın (açıdan bakış).
    CylinderLight tube(Vec3f(0, 2, 0), Vec3f(0.9f, 0.2f, 0.1f), 0.2f, Color3f(1.0f));
    const struct {
        Vec3f p, n;
    } cases[] = {
        {Vec3f(0, 0, 0), Vec3f(0, 1, 0)},
        {Vec3f(0.5f, 1.0f, 1.5f), Vec3f(0, 0.6f, -0.8f)},
        {Vec3f(2.0f, 3.0f, 0.4f), Vec3f(-1, -0.5f, 0)}, // ucun ötesinden eğik bakış
    };
    for (const auto& c : cases) {
        const SurfaceInteraction si = receiver(c.p, c.n.normalized());
        const float expected = tubeIrradianceQuadrature(tube, si.point, si.normal);
        ASSERT_GT(expected, 1e-4f);
        EXPECT_NEAR(estimateIrradiance(tube, si, 60000, 6), expected, 0.015f * expected);
    }
    // Uzantının içindeki alıcı (ρ ≤ r) dış yüzü göremez.
    const SurfaceInteraction axial = receiver(Vec3f(0, 2, 0) + tube.axis() * 3.0f, Vec3f(1, 0, 0));
    EXPECT_FALSE(tube.sampleLi(axial, Vec2f(0.5f, 0.5f)).isValid());
    EXPECT_NEAR(tube.power().r, PI * tube.area(), 1e-4f);
}

// ── Tek yüzlü ışıklar ─────────────────────────────────────────────────────

TEST(ShapeLights, OneSidedEmitsOnlyFromFront) {
    // Aşağı bakan dikdörtgen: u × v = (1,0,0) × (0,0,1) = (0,−1,0).
    AreaLight oneRect(Vec3f(-0.5f, 2, -0.5f), Vec3f(1, 0, 0), Vec3f(0, 0, 1), Color3f(4.0f), false);
    AreaLight twoRect(Vec3f(-0.5f, 2, -0.5f), Vec3f(1, 0, 0), Vec3f(0, 0, 1), Color3f(4.0f), true);
    DiskLight oneDisk(Vec3f(0, 2, 0), Vec3f(0.5f, 0, 0), Vec3f(0, 0, 0.5f), Color3f(4.0f), false);
    DiskLight twoDisk(Vec3f(0, 2, 0), Vec3f(0.5f, 0, 0), Vec3f(0, 0, 0.5f), Color3f(4.0f), true);
    const SurfaceInteraction below = receiver(Vec3f(0, 0, 0), Vec3f(0, 1, 0));
    const SurfaceInteraction above = receiver(Vec3f(0, 4, 0), Vec3f(0, -1, 0));
    for (const Light* l : {static_cast<const Light*>(&oneRect), static_cast<const Light*>(&oneDisk)}) {
        EXPECT_TRUE(l->sampleLi(below, Vec2f(0.4f, 0.6f)).isValid());
        EXPECT_FALSE(l->sampleLi(above, Vec2f(0.4f, 0.6f)).isValid());
        EXPECT_FLOAT_EQ(l->importance(above.point, above.normal), 0.0f);
        LightBounds lb;
        ASSERT_TRUE(l->bounds(lb));
        EXPECT_FALSE(lb.twoSided);
        EXPECT_FLOAT_EQ(lb.importance(above.point, above.normal), 0.0f);
        EXPECT_GT(lb.importance(below.point, below.normal), 0.0f);
    }
    for (const Light* l : {static_cast<const Light*>(&twoRect), static_cast<const Light*>(&twoDisk)})
        EXPECT_TRUE(l->sampleLi(above, Vec2f(0.4f, 0.6f)).isValid());
    EXPECT_NEAR(twoRect.power().r, 2.0f * oneRect.power().r, 1e-4f);
    EXPECT_NEAR(twoDisk.power().r, 2.0f * oneDisk.power().r, 1e-4f);
}

TEST(ShapeLights, CameraSeesOnlyEmittingSide) {
    for (const bool twoSided : {false, true}) {
        Scene scene;
        auto disk = std::make_shared<DiskLight>(Vec3f(0, 2, 0), Vec3f(0.5f, 0, 0), Vec3f(0, 0, 0.5f), Color3f(4.0f),
                                                twoSided);
        disk->setVisibleToCamera(true);
        scene.addLight(disk);
        auto rect = std::make_shared<AreaLight>(Vec3f(2.5f, 2, -0.5f), Vec3f(1, 0, 0), Vec3f(0, 0, 1), Color3f(6.0f),
                                                twoSided);
        rect->setVisibleToCamera(true);
        scene.addLight(rect);
        scene.buildAccelerator();
        PathTracer tracer(1, 100);
        IndependentSampler rng(9);
        EXPECT_NEAR(tracer.Li(Ray(Vec3f(0, 0, 0), Vec3f(0, 1, 0)), scene, rng).r, 4.0f, 1e-3f);
        EXPECT_NEAR(tracer.Li(Ray(Vec3f(0, 4, 0), Vec3f(0, -1, 0)), scene, rng).r, twoSided ? 4.0f : 0.0f, 1e-3f);
        EXPECT_NEAR(tracer.Li(Ray(Vec3f(3.1f, 0, 0.2f), Vec3f(0, 1, 0)), scene, rng).r, 6.0f, 1e-3f);
        EXPECT_NEAR(tracer.Li(Ray(Vec3f(3.1f, 4, 0.2f), Vec3f(0, -1, 0)), scene, rng).r, twoSided ? 6.0f : 0.0f,
                    1e-3f);
    }
    // Küre ve tüp: dıştan görünür, içeriden (iç yüz) karanlık.
    Scene scene;
    auto sphere = std::make_shared<SphereLight>(Vec3f(0, 2, 0), 0.5f, Color3f(3.0f));
    sphere->setVisibleToCamera(true);
    scene.addLight(sphere);
    auto tube = std::make_shared<CylinderLight>(Vec3f(4, 2, 0), Vec3f(0, 0, 1), 0.3f, Color3f(5.0f));
    tube->setVisibleToCamera(true);
    scene.addLight(tube);
    scene.buildAccelerator();
    PathTracer tracer(1, 100);
    IndependentSampler rng(10);
    EXPECT_NEAR(tracer.Li(Ray(Vec3f(0, 0, 0), Vec3f(0, 1, 0)), scene, rng).r, 3.0f, 1e-3f);
    EXPECT_NEAR(tracer.Li(Ray(Vec3f(0, 2, 0), Vec3f(0, 1, 0)), scene, rng).r, 0.0f, 1e-6f);
    EXPECT_NEAR(tracer.Li(Ray(Vec3f(4, 0, 0.5f), Vec3f(0, 1, 0)), scene, rng).r, 5.0f, 1e-3f);
    EXPECT_NEAR(tracer.Li(Ray(Vec3f(4, 2, 0), Vec3f(0, 1, 0)), scene, rng).r, 0.0f, 1e-6f);
}

// ── Path tracer: NEE + BSDF isabeti MIS ile analitik ışınımı verir ────────

TEST(ShapeLights, PathTracedFloorMatchesAnalytic) {
    // Lambert zemin (albedo ρ): L_o = ρ/π · E.
    const float rho = 0.5f;
    {
        auto sphere = std::make_shared<SphereLight>(Vec3f(0, 3, 0), 0.6f, Color3f(2.0f));
        const float e = PI * 2.0f * (0.36f / 9.0f);
        EXPECT_NEAR(floorRadiance(sphere, 6000, 21), rho / PI * e, 0.015f * rho / PI * e);
    }
    {
        auto disk = std::make_shared<DiskLight>(Vec3f(0, 1.5f, 0), Vec3f(0.9f, 0, 0), Vec3f(0, 0, 0.9f),
                                                Color3f(1.0f), false);
        const float e = PI * 0.81f / (2.25f + 0.81f);
        EXPECT_NEAR(floorRadiance(disk, 6000, 22), rho / PI * e, 0.015f * rho / PI * e);
    }
    {
        auto tube = std::make_shared<CylinderLight>(Vec3f(0, 1.2f, 0), Vec3f(0, 0, 1.0f), 0.25f, Color3f(1.0f));
        const float e = tubeIrradianceQuadrature(*tube, Vec3f(0, 0, 0), Vec3f(0, 1, 0));
        EXPECT_NEAR(floorRadiance(tube, 6000, 23), rho / PI * e, 0.015f * rho / PI * e);
    }
    {
        // Yukarı bakan tek yüzlü disk zemini aydınlatmaz; NEE de BSDF isabeti de sıfır.
        auto up = std::make_shared<DiskLight>(Vec3f(0, 1.5f, 0), Vec3f(0, 0, 0.9f), Vec3f(0.9f, 0, 0), Color3f(1.0f),
                                              false);
        ASSERT_GT(up->normal().y, 0.99f);
        EXPECT_FLOAT_EQ(floorRadiance(up, 500, 24), 0.0f);
    }
}
