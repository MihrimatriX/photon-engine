#include <gtest/gtest.h>
#include "materials/disney.h"
#include "core/image/image.h"
#include "geometry/surface_interaction.h"

using namespace photon;

TEST(ImageSampling, BilinearWrap) {
    Image img(2, 2);
    img.setPixel(0, 0, Color3f(1, 0, 0));
    img.setPixel(1, 0, Color3f(0, 1, 0));
    img.setPixel(0, 1, Color3f(0, 0, 1));
    img.setPixel(1, 1, Color3f(1, 1, 1));

    // Center of 2×2 → blend all four texels
    Color3f c = img.sampleBilinear(0.5f, 0.5f);
    EXPECT_GT(c.r, 0.2f);
    EXPECT_GT(c.g, 0.2f);
    EXPECT_GT(c.b, 0.2f);
    // Wrap: u=1.5 equals u=0.5
    Color3f c2 = img.sampleBilinear(1.5f, 0.5f);
    EXPECT_NEAR(c.r, c2.r, 1e-5f);
    EXPECT_NEAR(c.g, c2.g, 1e-5f);
    EXPECT_NEAR(c.b, c2.b, 1e-5f);
}

TEST(DisneyMaterial, ClearCoatAddsEnergy) {
    DisneyMaterial mat(Color3f(0.8f), 0.0f, 0.3f, 0.5f);
    SurfaceInteraction si;
    si.normal = Vec3f(0, 0, 1);
    si.tangent = Vec3f(1, 0, 0);
    si.uv = Vec2f(0.5f, 0.5f);

    Vec3f wo(0, 0, 1);
    Vec3f wi = Vec3f(0.2f, 0.1f, 0.9f).normalized();

    Color3f base = mat.eval(wo, wi, si);
    mat.setClearCoat(1.0f);
    mat.setClearCoatRoughness(0.05f);
    Color3f coated = mat.eval(wo, wi, si);
    EXPECT_GT(coated.r + coated.g + coated.b, base.r + base.g + base.b);
}

TEST(DisneyMaterial, ResolveUsesBaseWithoutMaps) {
    DisneyMaterial mat(Color3f(0.2f, 0.4f, 0.6f), 0.7f, 0.25f, 0.5f);
    SurfaceInteraction si;
    si.normal = Vec3f(0, 1, 0);
    si.uv = Vec2f(0, 0);
    auto p = mat.resolve(si);
    EXPECT_NEAR(p.baseColor.r, 0.2f, 1e-5f);
    EXPECT_NEAR(p.metallic, 0.7f, 1e-5f);
    EXPECT_NEAR(p.roughness, 0.25f, 1e-5f);
    EXPECT_NEAR(p.normal.y, 1.0f, 1e-5f);
}
