// Ürün/stüdyo özellikleri testleri: kamera odağı, dönüşüm, denoiser, zemin, malzeme ön ayarları, önizleme ışığı.

#include <gtest/gtest.h>
#include "ui/orbit_camera.h"
#include "engine/denoiser.h"
#include "scene/scene_graph.h"
#include "scene/material_library.h"
#include "materials/dielectric.h"
#include "materials/disney.h"
#include "preview/gl_preview.h"
#include "core/math/transform.h"
#include "lights/area_light.h"
#include "lights/directional_light.h"

using namespace photon;

TEST(OrbitCamera, ApertureUsesRadiusUntilFocusSet) {
    OrbitCamera cam;
    cam.radius = 1200.0f;
    cam.focusDistance = 5.5f;
    cam.aperture = 0.02f;
    cam.focusExplicit = false;
    EXPECT_NEAR(effectiveFocusDistance(cam), 1200.0f, 1e-3f);

    cam.focusExplicit = true;
    EXPECT_NEAR(effectiveFocusDistance(cam), 5.5f, 1e-3f);

    cam.aperture = 0.0f;
    EXPECT_NEAR(effectiveFocusDistance(cam), 5.5f, 1e-3f);
}

TEST(Transform, TranslationKeepsRotation) {
    Transform spun = Transform::rotateY(0.7f) * Transform::scale(Vec3f(2.0f, 1.0f, 0.5f));
    Transform moved = withTranslation(spun, Vec3f(3.0f, 4.0f, 5.0f));
    Vec3f v0 = spun.transformVector(Vec3f(1, 2, 3));
    Vec3f v1 = moved.transformVector(Vec3f(1, 2, 3));
    EXPECT_NEAR(v0.x, v1.x, 1e-4f);
    EXPECT_NEAR(v0.y, v1.y, 1e-4f);
    EXPECT_NEAR(v0.z, v1.z, 1e-4f);
    Vec3f p0 = spun.transformPoint(Vec3f(0, 0, 0));
    Vec3f p1 = moved.transformPoint(Vec3f(0, 0, 0));
    EXPECT_NEAR(p1.x - p0.x, 3.0f, 1e-4f);
    EXPECT_NEAR(p1.y - p0.y, 4.0f, 1e-4f);
    EXPECT_NEAR(p1.z - p0.z, 5.0f, 1e-4f);
}

// Image artık çözülmüş (ortalanmış) piksel tutar; örnek sayıları Film'e taşındı
// (Faz 1b Film/Image ayrımı). Bu iki test yeni API'ye uyarlandı.
TEST(Denoise, AovPointersKeepFallback) {
    Image img(3, 3);
    img.setPixel(1, 1, Color3f(4, 0, 0));
    Image albedo(3, 3);
    albedo.setPixel(1, 1, Color3f(1, 0, 0));
    Image normal(3, 3);
    normal.setPixel(1, 1, Color3f(0, 0, 1));
    EXPECT_TRUE(denoiseImage(img, &albedo, &normal));
    EXPECT_GT(img.getPixel(1, 1).r, 0.0f);
    EXPECT_EQ(albedo.getPixel(1, 1).r, 1.0f);
}

TEST(Denoise, CopyLeavesSourceAlone) {
    Image src(4, 4);
    src.setPixel(1, 1, Color3f(1, 0, 0));
    Image out = denoiseCopy(src);
    EXPECT_EQ(src.getPixel(1, 1).r, 1.0f);
    EXPECT_EQ(src.getPixel(0, 0).r, 0.0f);
    EXPECT_EQ(out.width(), src.width());
}

TEST(Ground, SitsUnderBounds) {
    AABB box(Vec3f(-1.0f, 0.4f, -0.5f), Vec3f(1.0f, 2.0f, 0.5f));
    GroundQuad g = placeGroundUnder(box);
    EXPECT_LT(g.corner.y, box.pMin.y);
    EXPECT_NEAR(g.corner.y, box.pMin.y - 0.02f, 1e-4f);
    EXPECT_GT(g.edgeU.x, (box.pMax.x - box.pMin.x) * 2.0f);
    EXPECT_NEAR(g.edgeU.y, 0.0f, 1e-6f);
}

TEST(MaterialPreset, EmissiveIsVisible) {
    MaterialLibrary lib;
    MaterialPreset led;
    led.id = "led_panel";
    led.baseColor = Color3f(1.0f, 0.95f, 0.85f);
    led.emissive = 5.0f;
    auto mat = lib.createMaterial(led);
    SurfaceInteraction si;
    EXPECT_FALSE(mat->emitted(si).isBlack());

    MaterialPreset plain;
    plain.id = "white_plastic";
    plain.baseColor = Color3f(0.8f);
    EXPECT_TRUE(lib.createMaterial(plain)->emitted(si).isBlack());
}

TEST(MaterialPreset, ClearGlassIsDielectric) {
    MaterialLibrary lib;
    MaterialPreset glass;
    glass.id = "clear_glass";
    glass.baseColor = Color3f(0.95f, 0.98f, 1.0f);
    auto mat = lib.createMaterial(glass);
    auto* di = dynamic_cast<Dielectric*>(mat.get());
    ASSERT_NE(di, nullptr);
    EXPECT_NEAR(di->ior(), 1.5f, 1e-5f);

    MaterialPreset frost;
    frost.id = "frosted_glass";
    frost.baseColor = Color3f(0.9f);
    frost.roughness = 0.45f;
    auto frosted = lib.createMaterial(frost);
    auto* rough = dynamic_cast<Dielectric*>(frosted.get());
    ASSERT_NE(rough, nullptr);
    EXPECT_GT(rough->roughness(), 0.0f);

    MaterialPreset brush;
    brush.id = "brushed_aluminum";
    auto metal = std::dynamic_pointer_cast<DisneyMaterial>(lib.createMaterial(brush));
    ASSERT_NE(metal, nullptr);
    EXPECT_NEAR(metal->anisotropy(), 0.7f, 1e-4f);

    MaterialPreset cloth;
    cloth.id = "linen";
    auto linen = std::dynamic_pointer_cast<DisneyMaterial>(lib.createMaterial(cloth));
    ASSERT_NE(linen, nullptr);
    EXPECT_GT(linen->sheen(), 0.0f);
}

TEST(Preview, LightDirFromScene) {
    std::vector<std::shared_ptr<Light>> none;
    Vec3f fallback = previewLightDirection(none);
    EXPECT_NEAR(fallback.x, 0.4f, 1e-5f);

    auto area = std::make_shared<AreaLight>(Vec3f(0, 2, 0), Vec3f(1, 0, 0), Vec3f(0, 0, 1), Color3f(1));
    std::vector<std::shared_ptr<Light>> areaOnly = {area};
    Vec3f fromArea = previewLightDirection(areaOnly);
    EXPECT_NEAR(fromArea.x, area->normal().x, 1e-4f);
    EXPECT_NEAR(fromArea.y, area->normal().y, 1e-4f);
    EXPECT_NEAR(fromArea.z, area->normal().z, 1e-4f);

    std::vector<std::shared_ptr<Light>> both = {
        areaOnly[0],
        std::make_shared<DirectionalLight>(Vec3f(0, -1, 0), Color3f(1))};
    Vec3f fromSun = previewLightDirection(both);
    EXPECT_NEAR(fromSun.y, 1.0f, 1e-4f);
    EXPECT_NEAR(fromSun.x, 0.0f, 1e-4f);
}
