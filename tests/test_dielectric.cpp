#include <gtest/gtest.h>
#include "materials/dielectric.h"
#include "geometry/sphere.h"
#include "core/math/frame.h"
#include "core/math/utils.h"
#include <cmath>

using namespace photon;

TEST(Dielectric, ExitRefractsTowardAir) {
    Dielectric glass(1.5f, Color3f(1.0f));
    Sphere sphere(Vec3f(0.0f), 1.0f, &glass);
    Ray ray(Vec3f(-0.15f, -0.2f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f));
    SurfaceInteraction isect;
    ASSERT_TRUE(sphere.intersect(ray, isect));

    Vec3f wo = (-ray.direction).normalized();
    Vec3f ng = isect.ng.normalized();
    EXPECT_GT(ng.dot(isect.point), 0.9f);
    EXPECT_LT(wo.dot(ng), 0.0f);
    EXPECT_LT(isect.normal.dot(ng), 0.0f);

    Vec3f wi;
    Color3f brdf;
    float pdf = 0.0f;
    ASSERT_TRUE(glass.sample(wo, isect, Vec2f(0.999f, 0.3f), wi, brdf, pdf));

    Frame frame(ng);
    Vec3f woLocal = frame.toLocal(wo);
    Vec3f wiLocal;
    ASSERT_TRUE(refractVec(-woLocal, Vec3f(0, 0, -1), 1.5f, wiLocal));
    Vec3f expected = frame.toWorld(wiLocal).normalized();
    EXPECT_NEAR(wi.x, expected.x, 1e-4f);
    EXPECT_NEAR(wi.y, expected.y, 1e-4f);
    EXPECT_NEAR(wi.z, expected.z, 1e-4f);
    EXPECT_GT(wi.dot(ng), 0.0f);

    Vec3f wrongLocal;
    ASSERT_TRUE(refractVec(-woLocal, Vec3f(0, 0, 1), 1.0f / 1.5f, wrongLocal));
    Vec3f wrong = frame.toWorld(wrongLocal).normalized();
    EXPECT_GT((wi - wrong).lengthSquared(), 1e-4f);
}

TEST(Dielectric, FrostedSampleRefracts) {
    Dielectric frost(1.5f, Color3f(1.0f), 0.35f);
    SurfaceInteraction si;
    si.ng = Vec3f(0, 1, 0);
    si.normal = si.ng;
    Vec3f wo(0, 1, 0);
    Vec3f wi;
    Color3f brdf;
    float pdf = 0.0f;
    ASSERT_TRUE(frost.sample(wo, si, Vec2f(0.5f, 0.1f), wi, brdf, pdf));
    EXPECT_LT(wi.dot(si.ng), 0.0f);
    EXPECT_GT(pdf, 0.0f);
    Vec3f mirror(0, 1, 0);
    EXPECT_GT((wi - mirror).lengthSquared(), 1e-3f);
}

TEST(Dielectric, ClearGlassReflectsAtGrazing) {
    Dielectric glass(1.5f, Color3f(1.0f));
    SurfaceInteraction si;
    si.ng = Vec3f(0, 1, 0);
    si.normal = si.ng;
    Vec3f wo = Vec3f(1.0f, 0.02f, 0.0f).normalized();
    Vec3f wi;
    Color3f brdf;
    float pdf = 0.0f;
    ASSERT_TRUE(glass.sample(wo, si, Vec2f(0.5f, 0.2f), wi, brdf, pdf));
    EXPECT_GT(wi.dot(si.ng), 0.0f);
    EXPECT_GT(pdf, 0.5f);
}
