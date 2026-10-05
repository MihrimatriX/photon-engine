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

TEST(Denoise, AovPointersKeepAovs) {
    Image img(3, 3);
    img.setPixel(1, 1, Color3f(4, 0, 0));
    Image albedo(3, 3);
    albedo.setPixel(1, 1, Color3f(1, 0, 0));
    Image normal(3, 3);
    normal.setPixel(1, 1, Color3f(0, 0, 1));
    EXPECT_TRUE(denoiseImage(img, &albedo, &normal));
    EXPECT_GT(img.getPixel(1, 1).r, 0.0f);
    EXPECT_FLOAT_EQ(albedo.getPixel(1, 1).r, 1.0f);
}

TEST(Denoise, CopyLeavesSourceAlone) {
    Image src(4, 4);
    src.setPixel(1, 1, Color3f(1, 0.5f, 0));
    Image out = denoiseCopy(src);
    EXPECT_FLOAT_EQ(src.getPixel(1, 1).g, 0.5f);
    EXPECT_FLOAT_EQ(src.getPixel(0, 0).r, 0.0f);
    EXPECT_EQ(out.width(), src.width());
    EXPECT_EQ(out.height(), src.height());
}

TEST(Ground, SitsUnderBounds) {
    AABB box(Vec3f(-1.0f, 0.4f, -0.5f), Vec3f(1.0f, 2.0f, 0.5f));
    GroundQuad g = placeGroundUnder(box);
    EXPECT_LT(g.corner.y, box.pMin.y);
    EXPECT_NEAR(g.corner.y, box.pMin.y, 1e-2f);
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

TEST(MaterialPreset, KindSelectsBsdf) {
    MaterialPreset glass;
    glass.kind = MaterialKind::Glass;
    glass.ior = 1.33f;
    glass.roughness = 0.0f;
    auto mat = MaterialLibrary::createMaterial(glass);
    auto* di = dynamic_cast<Dielectric*>(mat.get());
    ASSERT_NE(di, nullptr);
    EXPECT_NEAR(di->ior(), 1.33f, 1e-5f);
    EXPECT_EQ(di->roughness(), 0.0f);

    MaterialPreset frost = glass;
    frost.roughness = 0.45f;
    auto* rough = dynamic_cast<Dielectric*>(MaterialLibrary::createMaterial(frost).get());
    ASSERT_NE(rough, nullptr);
    EXPECT_GT(rough->roughness(), 0.0f);

    // Behaviour comes from the parameters, not from magic preset ids.
    MaterialPreset brush;
    brush.id = "brushed_aluminum";
    brush.metallic = 1.0f;
    brush.anisotropy = 0.75f;
    auto metal = std::dynamic_pointer_cast<DisneyMaterial>(MaterialLibrary::createMaterial(brush));
    ASSERT_NE(metal, nullptr);
    EXPECT_NEAR(metal->anisotropy(), 0.75f, 1e-4f);

    MaterialPreset plain;
    plain.id = "linen";
    auto linen = std::dynamic_pointer_cast<DisneyMaterial>(MaterialLibrary::createMaterial(plain));
    ASSERT_NE(linen, nullptr);
    EXPECT_EQ(linen->sheen(), 0.0f);
}

TEST(MaterialPreset, RoundTripThroughMaterial) {
    MaterialPreset p;
    p.baseColor = Color3f(0.2f, 0.4f, 0.6f);
    p.roughness = 0.3f;
    p.clearCoat = 0.7f;
    auto mat = MaterialLibrary::createMaterial(p);
    MaterialPreset back = MaterialLibrary::presetFromMaterial(*mat, "Test Boya");
    EXPECT_EQ(back.kind, MaterialKind::Generic);
    EXPECT_NEAR(back.baseColor.g, 0.4f, 1e-5f);
    EXPECT_NEAR(back.roughness, 0.3f, 1e-5f);
    EXPECT_NEAR(back.clearCoat, 0.7f, 1e-5f);
    EXPECT_EQ(back.id, "test_boya");

    auto copy = cloneMaterial(*mat);
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), mat.get());
}

TEST(MaterialLibrary, LoadsShippedPresets) {
    MaterialLibrary lib;
    ASSERT_TRUE(lib.loadFromDirectory(PHOTON_ASSETS_DIR "/materials"));
    const MaterialPreset* chrome = lib.findById("chrome");
    ASSERT_NE(chrome, nullptr);
    EXPECT_EQ(chrome->metallic, 1.0f);
    const MaterialPreset* glass = lib.findById("clear_glass");
    ASSERT_NE(glass, nullptr);
    EXPECT_EQ(glass->kind, MaterialKind::Glass);
    EXPECT_GE(lib.categories().size(), 5u);
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
