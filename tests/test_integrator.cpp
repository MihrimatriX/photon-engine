// PathTracer testleri: son sekmede enerji kaybı olmaması (lighting-m1), arkadaki ışığın
// geçirgen BSDF ile NEE'de sayılması (lighting-3), iki ışıkta MIS'in yansızlığı
// (lighting-9) ve NaN/Inf katkıların atlanması (lighting-10).

#include <gtest/gtest.h>
#include "engine/scene.h"
#include "integrators/path_tracer.h"
#include "lights/area_light.h"
#include "materials/disney.h"
#include "materials/lambertian.h"
#include "geometry/triangle.h"
#include "samplers/independent_sampler.h"
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

using namespace photon;

namespace {

void addFloor(Scene& scene, const Material* mat) {
    Vec3f n(0, 1, 0);
    Vec2f uv(0, 0);
    Vec3f p0(-3, 0, -3), p1(3, 0, -3), p2(3, 0, 3), p3(-3, 0, 3);
    scene.addShape(std::make_shared<Triangle>(p0, p2, p1, n, n, n, uv, uv, uv, mat));
    scene.addShape(std::make_shared<Triangle>(p0, p3, p2, n, n, n, uv, uv, uv, mat));
}

std::shared_ptr<AreaLight> squareLight(const Vec3f& center, float size, const Color3f& L) {
    Vec3f corner = center - Vec3f(0.5f * size, 0.0f, 0.5f * size);
    return std::make_shared<AreaLight>(corner, Vec3f(size, 0, 0), Vec3f(0, 0, size), L);
}

/// Doğrudan ışığın bağımsız referansı: yalnızca ışık örneklemesi, MIS yok, gölge yok
/// (sahnede örtücü yok). L_direct = Σ_ışık E[f · Li · |cos| / pdf].
Color3f directReference(const Scene& scene, const SurfaceInteraction& isect, const Vec3f& wo, int n) {
    IndependentSampler rng(42);
    Color3f sum = Color3f::black();
    for (const Light* light : scene.lights()) {
        for (int i = 0; i < n; ++i) {
            LightSample ls = light->sampleLi(isect, rng.get2D());
            if (!ls.isValid()) continue;
            Color3f f = isect.material->eval(wo, ls.wi, isect);
            sum += f * ls.Li * (std::abs(ls.wi.dot(isect.normal)) / (ls.pdf * static_cast<float>(n)));
        }
    }
    return sum;
}

Color3f averageLi(const PathTracer& tracer, const Scene& scene, const Ray& ray, int n) {
    IndependentSampler sampler(7);
    Color3f sum = Color3f::black();
    for (int i = 0; i < n; ++i) sum += tracer.Li(ray, scene, sampler);
    return sum / static_cast<float>(n);
}

/// eval/sample hep NaN döndüren bozuk malzeme (NaN korumasını sınamak için).
class NaNMaterial : public Material {
public:
    bool sample(const Vec3f&, const SurfaceInteraction& si, const Vec2f&, Vec3f& wi, Color3f& brdf,
                float& pdf) const override {
        wi = si.normal;
        brdf = Color3f(std::numeric_limits<float>::quiet_NaN());
        pdf = 1.0f;
        return true;
    }
    Color3f eval(const Vec3f&, const Vec3f&, const SurfaceInteraction&) const override {
        return Color3f(std::numeric_limits<float>::quiet_NaN());
    }
    float pdf(const Vec3f&, const Vec3f&, const SurfaceInteraction&) const override { return 1.0f; }
};

} // namespace

// maxDepth = 1 (yalnız doğrudan ışık) iki ışık altında tam doğrudan ışığı vermeli.
// Eskiden son noktada NEE MIS ağırlığıyla yapılıyor ama BSDF tamamlayıcısı izlenmiyordu
// → sonuç referansın altında kalıyordu. Işık seçim olasılığı da iki MIS ağırlığında var.
TEST(PathTracer, DirectLightingAtLastVertexIsComplete) {
    auto floorMat = std::make_shared<Lambertian>(Color3f(0.8f));
    Scene scene;
    addFloor(scene, floorMat.get());
    scene.addLight(squareLight(Vec3f(1.0f, 1.0f, 0.0f), 0.6f, Color3f(10.0f)));
    scene.addLight(squareLight(Vec3f(-0.5f, 2.0f, 0.8f), 1.5f, Color3f(3.0f)));
    scene.buildAccelerator();

    Ray ray(Vec3f(0.0f, 0.5f, 0.0f), Vec3f(0.0f, -1.0f, 0.0f));
    Ray probe = ray;
    SurfaceInteraction isect;
    ASSERT_TRUE(scene.intersect(probe, isect));
    Color3f ref = directReference(scene, isect, -ray.direction, 200000);

    PathTracer tracer(1, 100);
    Color3f L = averageLi(tracer, scene, ray, 40000);
    EXPECT_GT(ref.r, 0.1f);
    EXPECT_NEAR(L.r, ref.r, 0.02f * ref.r);
}

// Işık yüzeyin ARKASINDA, malzeme ince difüz geçirgen (f = baseColor/π alt yarıkürede).
// NEE |cos| kullanmalı; eskiden max(0, cos) yüzünden bu ışık NEE'de 0'dı, BSDF tarafı
// ise MIS ile kısılıyordu → enerji kaybı.
TEST(PathTracer, TransmissionSeesLightBehindSurface) {
    auto sheet = std::make_shared<DisneyMaterial>(Color3f(0.8f), 0.0f, 0.5f, 0.5f);
    sheet->setDiffuseTransmission(1.0f);
    Scene scene;
    addFloor(scene, sheet.get());
    scene.addLight(squareLight(Vec3f(1.0f, -1.0f, 0.0f), 0.6f, Color3f(10.0f)));
    scene.buildAccelerator();

    Ray ray(Vec3f(0.0f, 0.5f, 0.0f), Vec3f(0.0f, -1.0f, 0.0f));
    Ray probe = ray;
    SurfaceInteraction isect;
    ASSERT_TRUE(scene.intersect(probe, isect));
    Color3f ref = directReference(scene, isect, -ray.direction, 200000);

    PathTracer tracer(1, 100);
    Color3f L = averageLi(tracer, scene, ray, 40000);
    EXPECT_GT(ref.r, 0.05f);
    EXPECT_NEAR(L.r, ref.r, 0.03f * ref.r);
}

TEST(PathTracer, NonFiniteContributionsAreSkipped) {
    NaNMaterial bad;
    Scene scene;
    addFloor(scene, &bad);
    scene.addLight(squareLight(Vec3f(1.0f, 1.0f, 0.0f), 0.6f, Color3f(10.0f)));
    scene.buildAccelerator();

    Ray ray(Vec3f(0.0f, 0.5f, 0.0f), Vec3f(0.0f, -1.0f, 0.0f));
    IndependentSampler sampler(3);
    PathTracer tracer(4, 1);
    for (int i = 0; i < 64; ++i) {
        Color3f L = tracer.Li(ray, scene, sampler);
        ASSERT_TRUE(L.isFinite());
    }
}
