// test_area_light.cpp — Alan ışığı testleri: sampleLi ile pdfLi'nin tutarlılığı, kameranın ışığı
// doğrudan görmesi, NEE + BSDF yollarının MIS ile çift sayılmaması ve aynada ışığın görünmesi.
// pdf ile sample() tutarlılığı bozulursa MIS ağırlıklarının toplamı 1 olmaz ve görüntü
// sistematik olarak yanlış parlaklıkta (fazla ya da eksik) çıkar.
#include "gtest/gtest.h"
#include "lights/area_light.h"
#include "engine/scene.h"
#include "integrators/path_tracer.h"
#include "materials/lambertian.h"
#include "materials/mirror.h"
#include "geometry/triangle.h"
#include "samplers/sampler.h"
#include "samplers/independent_sampler.h"
#include "core/math/utils.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace photon;

namespace {

// Önceden yazılmış sayıları sırayla döndüren sahte örnekleyici. Path tracer'ın her rastgele kararı
// (ışık seçimi, ışık üzerindeki nokta, BSDF yönü) böylece sabitlenir ve tek bir yol, elle
// hesaplanan beklenen değerle hassas karşılaştırılabilir. Liste bitince 0.5 döner.
class ScriptedSampler : public Sampler {
public:
    explicit ScriptedSampler(std::vector<float> xs) : m_xs(std::move(xs)) {}

    float get1D() override {
        if (m_i >= m_xs.size()) return 0.5f;
        return m_xs[m_i++];
    }

    Vec2f get2D() override { return Vec2f(get1D(), get1D()); }

    std::unique_ptr<Sampler> clone(uint64_t) const override {
        auto copy = std::make_unique<ScriptedSampler>(m_xs);
        copy->m_i = m_i;
        return copy;
    }

    void startPixel(int, int) override {}
    void startSample(int) override {}

private:
    std::vector<float> m_xs;
    size_t m_i = 0;
};

void addQuad(Scene& scene, const Vec3f& p, const Vec3f& u, const Vec3f& v,
             const Vec3f& n, const Material* mat) {
    Vec3f p0 = p;
    Vec3f p1 = p + u;
    Vec3f p2 = p + u + v;
    Vec3f p3 = p + v;
    Vec2f uv(0.0f, 0.0f);
    scene.addShape(std::make_shared<Triangle>(p0, p1, p2, n, n, n, uv, uv, uv, mat));
    scene.addShape(std::make_shared<Triangle>(p0, p2, p3, n, n, n, uv, uv, uv, mat));
}

std::shared_ptr<AreaLight> studioLight() {
    return std::make_shared<AreaLight>(
        Vec3f(0.0f, 3.0f, 0.0f), Vec3f(-1.4f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 1.0f), Color3f(18.0f));
}

// Interior of one triangle. The quad diagonal is s == t, which is a flaky hit.
Vec3f interiorPoint(const AreaLight& light) {
    return light.position() + light.u() * 0.25f + light.v() * 0.75f;
}

} // namespace

TEST(AreaLight, SampleLiHitsUpwardSoftbox) {
    auto light = studioLight();
    SurfaceInteraction si;
    si.point = Vec3f(0.0f, 0.0f, 0.0f);
    si.normal = Vec3f(0.0f, 1.0f, 0.0f);

    // Studio quads are built with u × v = +Y, and wi from a receiver below is also +Y.
    EXPECT_GT(light->normal().y, 0.0f);
    LightSample ls = light->sampleLi(si, Vec2f(0.5f, 0.5f));
    EXPECT_GT(ls.wi.y, 0.0f);
    EXPECT_TRUE(ls.isValid());
    EXPECT_NEAR(ls.Li.r, 18.0f, 1e-5f);

    Vec3f pLight = si.point + ls.wi * ls.distance;
    EXPECT_NEAR(light->pdfLi(si.point, pLight), ls.pdf, 1e-4f);
}

TEST(AreaLight, CameraSeesEmissiveQuad) {
    Scene scene;
    auto light = studioLight();
    scene.addLight(light);
    scene.buildAccelerator();

    Vec3f aim = interiorPoint(*light);
    Ray ray(Vec3f(aim.x, aim.y - 1.0f, aim.z), Vec3f(0.0f, 1.0f, 0.0f));
    ScriptedSampler sampler({0.5f, 0.5f});
    PathTracer tracer(2, 8);
    Color3f L = tracer.Li(ray, scene, sampler);

    EXPECT_NEAR(L.r, light->radiance().r, 1e-3f);
    EXPECT_NEAR(L.g, light->radiance().g, 1e-3f);
    EXPECT_NEAR(L.b, light->radiance().b, 1e-3f);
}

// Aynı ışık iki yoldan bulunabilir: NEE (ışığı örnekle) ve BSDF yönünün ışığa çarpması. İkisi de
// tam ağırlıkla eklenirse ışık iki kez sayılır. MIS (güç sezgiseli, Veach 1997) katkıyı
// w_nee + w_bsdf = 1 olacak şekilde böler. Test beklenen değeri adım adım elle kurar.
TEST(AreaLight, DiffuseFloorLitWithoutDoubleCount) {
    auto floorMat = std::make_shared<Lambertian>(Color3f::white());
    Scene scene;
    addQuad(scene, Vec3f(-2.0f, 0.0f, -2.0f), Vec3f(4.0f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 4.0f),
            Vec3f(0.0f, 1.0f, 0.0f), floorMat.get());
    auto light = studioLight();
    scene.addLight(light);
    scene.buildAccelerator();

    Vec3f aim = interiorPoint(*light);
    Ray ray(Vec3f(aim.x, 1.0f, aim.z), Vec3f(0.0f, -1.0f, 0.0f));

    Ray probe = ray;
    SurfaceInteraction isect;
    ASSERT_TRUE(scene.intersect(probe, isect));
    Vec3f wo = -ray.direction;

    LightSample ls = light->sampleLi(isect, Vec2f(0.5f, 0.5f));
    ASSERT_TRUE(ls.isValid());
    Color3f brdf = floorMat->eval(wo, ls.wi, isect);
    float cosTheta = std::max(0.0f, ls.wi.dot(isect.normal));
    float bsdfPdf = floorMat->pdf(wo, ls.wi, isect);
    ASSERT_GT(bsdfPdf, 0.0f);
    float wNee = powerHeuristic(1, ls.pdf, 1, bsdfPdf);
    float wBsdf = powerHeuristic(1, bsdfPdf, 1, ls.pdf);
    EXPECT_NEAR(wNee + wBsdf, 1.0f, 1e-4f);
    EXPECT_GT(wNee, 0.0f);
    EXPECT_LT(wNee, 1.0f);

    Vec3f wi;
    Color3f brdfS;
    float pdfS;
    ASSERT_TRUE(floorMat->sample(wo, isect, Vec2f(0.5f, 0.5f), wi, brdfS, pdfS));
    Vec3f origin = isect.point + isect.normal * (wi.dot(isect.normal) > 0.0f ? 1e-4f : -1e-4f);
    Ray bounce(origin, wi);
    SurfaceInteraction lightHit;
    ASSERT_TRUE(scene.intersect(bounce, lightHit));
    Color3f emitted = lightHit.material->emitted(lightHit);
    ASSERT_FALSE(emitted.isBlack());

    float lightPdf = light->pdfLi(isect.point, lightHit.point);
    ASSERT_GT(lightPdf, 0.0f);
    float wHit = powerHeuristic(1, pdfS, 1, lightPdf);
    EXPECT_NEAR(wHit + powerHeuristic(1, lightPdf, 1, pdfS), 1.0f, 1e-4f);

    Color3f nee = brdf * ls.Li * cosTheta * wNee / ls.pdf;
    Color3f throughput = brdfS * std::abs(wi.dot(isect.normal)) / pdfS;
    Color3f emit = throughput * emitted * wHit;
    Color3f expected = nee + emit;
    // weight 1 on both NEE and the bounce is the double count this slice removes
    Color3f doubled = brdf * ls.Li * cosTheta / ls.pdf + throughput * emitted;
    EXPECT_GT(std::abs(doubled.r - expected.r), 1.0f);

    // get1D light pick, get2D area sample (0.5, 0.5), get2D BSDF sample (0.5, 0.5)
    ScriptedSampler sampler({0.0f, 0.5f, 0.5f, 0.5f, 0.5f});
    PathTracer tracer(2, 8, 0.0f, 1);
    Color3f L = tracer.Li(ray, scene, sampler);
    EXPECT_GT(L.luminance(), 0.05f);
    // Tight enough that NEE weight = 1 (about 0.002 too bright here) does not pass.
    EXPECT_NEAR(L.r, expected.r, 1e-4f);
    EXPECT_NEAR(L.g, expected.g, 1e-4f);
    EXPECT_NEAR(L.b, expected.b, 1e-4f);

    IndependentSampler rng(1);
    PathTracer avgTracer(3, 5);
    Color3f acc = Color3f::black();
    const int n = 8;
    for (int i = 0; i < n; ++i) acc += avgTracer.Li(ray, scene, rng);
    acc = acc / static_cast<float>(n);
    EXPECT_GT(acc.luminance(), 0.05f);
}

TEST(AreaLight, MirrorSeesLightGeometry) {
    auto mirror = std::make_shared<Mirror>(Color3f::white());
    Scene scene;
    addQuad(scene, Vec3f(-2.0f, 0.0f, -2.0f), Vec3f(4.0f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 4.0f),
            Vec3f(0.0f, 1.0f, 0.0f), mirror.get());
    auto light = studioLight();
    scene.addLight(light);
    scene.buildAccelerator();

    Vec3f aim = interiorPoint(*light);
    Ray ray(Vec3f(aim.x, 1.0f, aim.z), Vec3f(0.0f, -1.0f, 0.0f));
    ScriptedSampler sampler({0.5f, 0.5f, 0.5f, 0.5f});
    PathTracer tracer(2, 8);
    Color3f L = tracer.Li(ray, scene, sampler);
    EXPECT_GT(L.luminance(), 1.0f);
    EXPECT_NEAR(L.r, light->radiance().r, 1e-2f);
}
