// Sağlamlık testleri (F5.13, F5.9, F5.10): aynı sahne 0.01×, 100× ve 10000× ölçekte ya da
// orijinden binlerce birim uzakta aynı görüntüyü vermeli. CAD modelleri milimetreyle (bir araba
// 4500 birim), mücevher metreyle (bir yüzük 0.02 birim) gelir; sabit ışın ofsetleri (1e-4)
// büyük ölçekte kendi kendini gölgeleme (acne), küçük ölçekte ışık sızması yapar.
// Ayrıca kaba üçgenli yumuşak (vertex normalli) modellerde gölge terminatörü testi.

#include <gtest/gtest.h>
#include "camera/perspective_camera.h"
#include "engine/embree_accel.h"
#include "engine/renderer.h"
#include "engine/scene.h"
#include "geometry/mesh.h"
#include "geometry/sphere.h"
#include "lights/area_light.h"
#include "lights/environment_light.h"
#include "materials/disney.h"
#include "materials/lambertian.h"
#include "geometry/ray_spawn.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace photon;

namespace {

/// Kaba UV küresi (yumuşak normaller): gölge terminatörünün klasik kurbanı.
std::shared_ptr<TriangleMesh> lowPolySphere(const Vec3f& c, float r, int slices, int stacks, const Material* m) {
    std::vector<Vec3f> pos, nor;
    std::vector<Vec2f> uv;
    std::vector<uint32_t> idx;
    for (int i = 0; i <= stacks; ++i) {
        const float th = PI * static_cast<float>(i) / static_cast<float>(stacks);
        for (int j = 0; j <= slices; ++j) {
            const float ph = TWO_PI * static_cast<float>(j) / static_cast<float>(slices);
            const Vec3f n(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph));
            pos.push_back(c + n * r);
            nor.push_back(n);
            uv.emplace_back(static_cast<float>(j) / slices, static_cast<float>(i) / stacks);
        }
    }
    for (int i = 0; i < stacks; ++i)
        for (int j = 0; j < slices; ++j) {
            const uint32_t a = static_cast<uint32_t>(i * (slices + 1) + j), b = a + static_cast<uint32_t>(slices + 1);
            idx.insert(idx.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    return std::make_shared<TriangleMesh>(pos, nor, uv, idx, m);
}

struct TestScene {
    std::unique_ptr<Scene> scene = std::make_unique<Scene>();
    std::unique_ptr<PerspectiveCamera> camera;
};

TestScene buildScene(float s, const Vec3f& offset) {
    static const Lambertian floorMat(Color3f(0.7f));
    static const DisneyMaterial plastic(Color3f(0.8f, 0.3f, 0.2f), 0.0f, 0.4f, 0.5f);
    static const DisneyMaterial metal(Color3f(0.9f, 0.8f, 0.6f), 1.0f, 0.3f, 0.5f);
    TestScene t;
    auto P = [&](float x, float y, float z) { return Vec3f(x, y, z) * s + offset; };
    const Vec3f up(0, 1, 0);
    std::vector<Vec3f> fp = {P(-3, 0, -3), P(3, 0, -3), P(3, 0, 3), P(-3, 0, 3)};
    t.scene->addShape(std::make_shared<TriangleMesh>(fp, std::vector<Vec3f>{up, up, up, up},
                                                     std::vector<Vec2f>(4, Vec2f(0.0f)),
                                                     std::vector<uint32_t>{0, 2, 1, 0, 3, 2}, &floorMat));
    t.scene->addShape(lowPolySphere(P(-0.6f, 0.5f, 0.0f), 0.5f * s, 10, 6, &plastic));
    t.scene->addShape(std::make_shared<Sphere>(P(0.7f, 0.4f, 0.1f), 0.4f * s, &metal));
    // Yan ışık: kaba kürenin terminatörü kadrajda.
    t.scene->addLight(std::make_shared<AreaLight>(P(-2.5f, 1.2f, -0.4f), Vec3f(0, 0, 0.8f) * s, Vec3f(0, 0.8f, 0) * s,
                                                  Color3f(10.0f)));
    t.scene->setEnvironment(std::make_shared<EnvironmentLight>(nullptr, 0.0f, 0.15f));
    t.scene->buildAccelerator();
    t.camera = std::make_unique<PerspectiveCamera>(P(0.0f, 1.4f, 4.0f), P(0.0f, 0.4f, 0.0f), up, 35.0f, 4.0f / 3.0f);
    return t;
}

Image renderAt(float s, const Vec3f& offset) {
    TestScene t = buildScene(s, offset);
    RenderSettings rs;
    rs.width = 64;
    rs.height = 48;
    rs.samplesPerPixel = 32;
    rs.maxBounces = 4;
    rs.filter = PixelFilter{PixelFilterType::Box, 1.0f};
    Renderer r;
    return r.render(*t.scene, *t.camera, rs);
}

float meanLum(const Image& img) {
    double sum = 0.0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) sum += img.getPixel(x, y).luminance();
    return static_cast<float>(sum / (img.width() * img.height()));
}

/// Göreli ortalama kare hata (compare_exr.py ile aynı tanım).
float relMse(const Image& a, const Image& ref) {
    double sum = 0.0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) {
            const float d = a.getPixel(x, y).luminance() - ref.getPixel(x, y).luminance();
            const float r = ref.getPixel(x, y).luminance();
            sum += d * d / (r * r + 0.01f);
        }
    return static_cast<float>(sum / (a.width() * a.height()));
}

} // namespace

/// Her iki kesişim arka ucu: yerleşik BVH ve (derlemede varsa) Embree. Işın başlangıç ofseti
/// kesişim noktasının hata payına dayanır; ikisi de aynı fillHit'ten geçse de Embree'nin t ve
/// barycentric hesabı farklıdır, bu yüzden ayrı ayrı denenir.
template <typename F>
void forEachBackend(F&& body) {
    for (const bool own : {true, false}) {
        if (!own && !EmbreeAccel::available()) continue;
        SCOPED_TRACE(own ? "yerleşik BVH" : "Embree");
        Scene::setPreferOwnBvh(own);
        body(own);
    }
    Scene::setPreferOwnBvh(false);
}

TEST(Robustness, ImageIsScaleInvariant) {
    forEachBackend([](bool own) {
        const Image ref = renderAt(1.0f, Vec3f(0.0f));
        const float refMean = meanLum(ref);
        ASSERT_GT(refMean, 0.01f);
        struct Variant {
            float scale;
            Vec3f offset;
        };
        for (const Variant v : {Variant{0.01f, Vec3f(0.0f)}, Variant{100.0f, Vec3f(0.0f)},
                                Variant{10000.0f, Vec3f(0.0f)}, Variant{1.0f, Vec3f(3000.0f, 0.0f, -2000.0f)}}) {
            const Image img = renderAt(v.scale, v.offset);
            const float m = meanLum(img);
            EXPECT_NEAR(m / refMean, 1.0f, 0.02f) << "scale " << v.scale << " offset " << v.offset.x;
            EXPECT_LT(relMse(img, ref), 0.01f) << "scale " << v.scale << " offset " << v.offset.x;
        }
        (void)own;
    });
}

// Gölge terminatörü (Hanika 2021): kaba (10×6) yumuşak normalli dışbükey küre. Yumuşak normal
// ışığa "bakıyor" (N·L > 0) dediği her noktada gölge ışını engelsiz olmalı (dışbükey nesne
// kendini gölgeleyemez). Düzeltmesiz, terminatör yakınında gölge ışınları kaba üçgenin kendisine
// ya da komşusuna çarpar: aydınlık/gölge sınırı basamaklı kararır.
TEST(Robustness, ShadowTerminatorFollowsSmoothNormals) {
    static const DisneyMaterial white(Color3f(0.8f), 0.0f, 0.6f, 0.5f);
    forEachBackend([](bool own) {
        Scene scene;
        scene.addShape(lowPolySphere(Vec3f(0.0f), 1.0f, 10, 6, &white));
        scene.buildAccelerator();
        EXPECT_EQ(scene.usesEmbree(), !own);
        const Vec3f lightPos(-4.0f, -0.3f, 0.0f);
        int lit = 0, blocked = 0;
        for (int y = 0; y < 200; ++y)
            for (int x = 0; x < 200; ++x) {
                Ray ray(Vec3f(-1.0f + 0.01f * x, -1.0f + 0.01f * y, 4.0f), Vec3f(0, 0, -1));
                SurfaceInteraction si;
                if (!scene.intersect(ray, si)) continue;
                const Vec3f toL = (lightPos - si.point).normalized();
                if (si.normal.dot(toL) <= 0.0f) continue; // yumuşak normale göre gölgede
                ++lit;
                Ray shadow;
                if (!spawnRayTo(si, lightPos, shadow) || scene.intersectAny(shadow)) ++blocked;
            }
        ASSERT_GT(lit, 1000);
        std::printf("terminator (%s): %d of %d lit points self-shadowed\n", own ? "BVH" : "Embree", blocked, lit);
        EXPECT_LT(blocked, lit / 200);
    });
}
