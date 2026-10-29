// Kamera ve ışık araçları (Marmoset / D5 / Maverick tarzı) testleri:
//   - renk sıcaklığı (Kelvin): parlaklığı 1, düşük K sıcak (kırmızı > mavi), yüksek K soğuk,
//     6500 K nötre yakın, sıcaklık arttıkça kırmızı/mavi oranı düzenli düşer; ışığa uygulanır ve
//     projeyle birlikte kaydedilir;
//   - lens kaydırma: merkez ışını kare yüksekliği cinsinden tam istenen kadar kayar, kamera
//     yatay kaldıkça dikey çizgiler ekranda dikey kalır (iki noktalı perspektif);
//   - parlama yerleştirme: tıklanan noktaya ışık yerleştirildikten sonra krom kürenin render'ında
//     en parlak nokta tıklanan piksele düşer (gerçek renderer ile, ayna yansıması kuralı sınanır).
#include <gtest/gtest.h>
#include "camera/perspective_camera.h"
#include "camera/thin_lens_camera.h"
#include "core/image/imager.h"
#include "engine/renderer.h"
#include "engine/scene.h"
#include "geometry/sphere.h"
#include "lights/environment_light.h"
#include "materials/disney.h"
#include "scene/document.h"
#include "scene/project_io.h"
#include "scene/scene_graph.h"

#include <cmath>

using namespace photon;

TEST(Kelvin, WarmToCoolWithUnitLuminance) {
    float lastRatio = 1e9f;
    for (const float k : {1900.0f, 2700.0f, 3200.0f, 4000.0f, 5600.0f, 6500.0f, 8000.0f, 12000.0f}) {
        const Color3f c = temperatureColor(k);
        SCOPED_TRACE(k);
        EXPECT_NEAR(c.luminance(), 1.0f, 1e-3f);
        const float ratio = c.r / std::max(1e-4f, c.b);
        EXPECT_LT(ratio, lastRatio); // sıcaklık arttıkça mavi baskınlaşır
        lastRatio = ratio;
    }
    const Color3f tungsten = temperatureColor(2700.0f);
    EXPECT_GT(tungsten.r, 1.5f * tungsten.b);
    const Color3f d65 = temperatureColor(6500.0f);
    EXPECT_NEAR(d65.r, d65.b, 0.12f); // sRGB beyaz noktası D65: nötre yakın
    EXPECT_NEAR(d65.g, 1.0f, 0.1f);
    const Color3f sky = temperatureColor(12000.0f);
    EXPECT_GT(sky.b, sky.r);
    // Aralık dışı değerler kenetlenir, NaN üretmez.
    const Color3f lo = temperatureColor(10.0f), hi = temperatureColor(1e6f);
    EXPECT_TRUE(std::isfinite(lo.r) && std::isfinite(hi.b));
}

TEST(Kelvin, AppliedToLightAndSavedWithProject) {
    LightDesc d;
    d.type = LightDesc::Type::Point;
    d.position = Vec3f(0, 1, 0);
    d.color = Color3f(0.0f, 1.0f, 0.0f); // Kelvin açıkken RGB yok sayılır
    d.intensity = 4.0f;
    d.temperatureK = 2700.0f;
    const auto lights = buildLights({d});
    ASSERT_EQ(lights.size(), 1u);
    SurfaceInteraction si;
    si.point = Vec3f(0, 0, 0);
    si.normal = Vec3f(0, 1, 0);
    const LightSample s = lights[0]->sampleLi(si, Vec2f(0.5f, 0.5f));
    ASSERT_TRUE(s.isValid());
    const Color3f expect = temperatureColor(2700.0f);
    EXPECT_NEAR(s.Li.r / s.Li.g, expect.r / expect.g, 1e-3f);
    EXPECT_NEAR(s.Li.b / s.Li.g, expect.b / expect.g, 1e-3f);

    SceneGraph g;
    ProjectData data;
    data.lights = {d};
    const std::string text = projectJsonText(g, data, ".");
    SceneGraph g2;
    ProjectData back;
    std::string err;
    ASSERT_TRUE(loadProjectJson(text, ".", g2, back, &err)) << err;
    ASSERT_EQ(back.lights.size(), 1u);
    EXPECT_FLOAT_EQ(back.lights[0].temperatureK, 2700.0f);
    // Kelvin kapalı ışıkta alan yazılmaz, okununca 0 kalır.
    data.lights[0].temperatureK = 0.0f;
    ASSERT_TRUE(loadProjectJson(projectJsonText(g, data, "."), ".", g2, back, &err)) << err;
    EXPECT_FLOAT_EQ(back.lights[0].temperatureK, 0.0f);
}

TEST(LensShift, CenterRayMovesByFrameHeights) {
    const float fov = 40.0f, tanHalf = std::tan(0.5f * fov * DEG_TO_RAD);
    for (const float shift : {0.0f, 0.15f, -0.3f}) {
        PerspectiveCamera cam(Vec3f(0, 0, 0), Vec3f(0, 0, -1), Vec3f(0, 1, 0), fov, 1.5f);
        cam.setShift(0.0f, shift);
        const Ray r = cam.generateRay(0.5f, 0.5f, Vec2f(0.5f, 0.5f));
        // Kare yüksekliği (birim uzaklıkta) 2·tan(fov/2); merkez ışını shift kare yüksekliği kayar.
        EXPECT_NEAR(r.direction.y / -r.direction.z, 2.0f * tanHalf * shift, 1e-4f) << shift;
        EXPECT_NEAR(r.direction.x, 0.0f, 1e-5f);
    }
    // Yatay kaydırma da kare yüksekliği cinsindendir.
    PerspectiveCamera cx(Vec3f(0, 0, 0), Vec3f(0, 0, -1), Vec3f(0, 1, 0), fov, 2.0f);
    cx.setShift(0.25f, 0.0f);
    const Ray rx = cx.generateRay(0.5f, 0.5f, Vec2f(0.5f, 0.5f));
    EXPECT_NEAR(rx.direction.x / -rx.direction.z, 2.0f * tanHalf * 0.25f, 1e-4f);
    // İnce mercek kamerası da aynı kaydırmayı uygular (odak düzleminde).
    ThinLensCamera tl(Vec3f(0, 0, 0), Vec3f(0, 0, -1), Vec3f(0, 1, 0), fov, 1.5f, 0.0f, 3.0f);
    tl.setShift(0.0f, 0.2f);
    const Ray rt = tl.generateRay(0.5f, 0.5f, Vec2f(0.5f, 0.5f));
    EXPECT_NEAR(rt.direction.y / -rt.direction.z, 2.0f * tanHalf * 0.2f, 1e-4f);
}

TEST(LensShift, LevelCameraKeepsVerticalsVertical) {
    // Bina köşesi: (2, 0..10, −8). Kamera göz hizasında, yatay bakıyor, tepeyi kadraja almak için
    // yukarı kaydırılmış. Köşenin alt ve üst noktası ekranda aynı x'te olmalı.
    PerspectiveCamera cam(Vec3f(0, 1.6f, 0), Vec3f(0, 1.6f, -1), Vec3f(0, 1, 0), 50.0f, 1.0f);
    cam.setShift(0.0f, 0.3f);
    auto screenX = [&](const Vec3f& p) {
        // u'yu ikili aramayla bul: ışının p yönündeki x/z oranı.
        const float target = p.x / -p.z;
        float lo = 0.0f, hi = 1.0f;
        for (int i = 0; i < 40; ++i) {
            const float mid = 0.5f * (lo + hi);
            const Ray r = cam.generateRay(mid, 0.5f, Vec2f(0.5f, 0.5f));
            (r.direction.x / -r.direction.z < target ? lo : hi) = mid;
        }
        return 0.5f * (lo + hi);
    };
    // Kamera yatayken ışının x/z oranı v'den bağımsızdır: aynı u alt ve üstte aynı x'i verir.
    const float uBottom = screenX(Vec3f(2.0f, 0.0f - 1.6f, -8.0f));
    const float uTop = screenX(Vec3f(2.0f, 10.0f - 1.6f, -8.0f));
    EXPECT_NEAR(uBottom, uTop, 1e-4f);
    for (const float v : {0.0f, 0.3f, 1.0f}) {
        const Ray r = cam.generateRay(uBottom, v, Vec2f(0.5f, 0.5f));
        EXPECT_NEAR(r.direction.x / -r.direction.z, 2.0f / 8.0f, 1e-3f) << v;
    }
}

TEST(Highlight, BrightestReflectionLandsOnClickedPixel) {
    // Karanlık ortamda krom küre; kameradan görünmeyen küçük bir softbox. Tıklanan piksel küre
    // üstündeki bir nokta; ışık yerleştirildikten sonra render'daki en parlak piksel orası olmalı.
    static const DisneyMaterial chrome(Color3f(0.95f), 1.0f, 0.03f, 0.5f);
    const int W = 64, H = 48;
    const Vec3f eye(0.0f, 0.6f, 4.0f);
    PerspectiveCamera cam(eye, Vec3f(0, 0, 0), Vec3f(0, 1, 0), 30.0f, static_cast<float>(W) / H);
    for (const auto& click : {std::pair<int, int>{46, 17}, std::pair<int, int>{21, 33}}) {
        SCOPED_TRACE(click.first);
        Scene scene;
        scene.addShape(std::make_shared<Sphere>(Vec3f(0, 0, 0), 1.0f, &chrome));
        scene.setEnvironment(std::make_shared<EnvironmentLight>(nullptr, 0.0f, 0.0f));
        scene.buildAccelerator();
        const float u = (static_cast<float>(click.first) + 0.5f) / W;
        const float v = 1.0f - (static_cast<float>(click.second) + 0.5f) / H;
        Ray ray = cam.generateRay(u, v, Vec2f(0.5f, 0.5f));
        SurfaceInteraction hit;
        ASSERT_TRUE(scene.intersect(ray, hit));

        LightDesc light;
        light.type = LightDesc::Type::Area;
        light.width = light.height = 0.15f;
        light.intensity = 200.0f;
        light.visibleToCamera = false;
        light.position = Vec3f(0, 5, 0);
        const Vec3f r = placeLightForHighlight(light, eye, hit.point, hit.normal, 3.0f);
        EXPECT_NEAR((light.position - hit.point).length(), (Vec3f(0, 5, 0) - hit.point).length(), 1e-3f); // uzaklık korunur
        EXPECT_NEAR(r.length(), 1.0f, 1e-4f);
        for (const auto& l : buildLights({light})) scene.addLight(l);
        scene.buildAccelerator();

        RenderSettings rs;
        rs.width = W;
        rs.height = H;
        rs.samplesPerPixel = 16;
        rs.maxBounces = 2;
        rs.filter = PixelFilter{PixelFilterType::Box, 1.0f};
        Renderer renderer;
        const Image img = renderer.render(scene, cam, rs);
        int bx = -1, by = -1;
        float best = -1.0f;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (img.getPixel(x, y).luminance() > best) {
                    best = img.getPixel(x, y).luminance();
                    bx = x;
                    by = y;
                }
        EXPECT_GT(best, 0.5f);
        EXPECT_LE(std::abs(bx - click.first), 1);
        EXPECT_LE(std::abs(by - click.second), 1);
    }
}

TEST(Highlight, DirectionalLightTakesReflectedDirection) {
    LightDesc sun;
    sun.type = LightDesc::Type::Directional;
    // Yukarı bakan düz yüzey, göz önde ve yukarıda: yansıma yukarı-geri.
    const Vec3f eye(0, 2, 4), p(0, 0, 0);
    const Vec3f r = placeLightForHighlight(sun, eye, p, Vec3f(0, -1, 0) /* ters normal düzeltilir */, 1.0f);
    const Vec3f expect = Vec3f(0, 2, -4).normalized();
    EXPECT_NEAR(r.x, expect.x, 1e-4f);
    EXPECT_NEAR(r.y, expect.y, 1e-4f);
    EXPECT_NEAR(r.z, expect.z, 1e-4f);
    const Vec3f from = sphericalDirection(sun.azimuthDeg, sun.elevationDeg);
    EXPECT_NEAR(from.dot(expect), 1.0f, 1e-4f);
}
