// Küre kesişimi, sınır kutusu ve teğet yönü testleri.

#include "gtest/gtest.h"
#include "geometry/sphere.h"
#include "materials/lambertian.h"

using namespace photon;

TEST(SphereTest, RayIntersection) {
    auto mat = std::make_shared<Lambertian>(Color3f(1.0f));
    Sphere sphere(Vec3f(0, 0, 0), 1.0f, mat.get());

    // Ray hitting from front
    Ray r(Vec3f(0, 0, -5), Vec3f(0, 0, 1));
    SurfaceInteraction isect;
    EXPECT_TRUE(sphere.intersect(r, isect));
    EXPECT_FLOAT_EQ(isect.t, 4.0f);
    EXPECT_FLOAT_EQ(isect.point.x, 0.0f);
    EXPECT_FLOAT_EQ(isect.point.y, 0.0f);
    EXPECT_FLOAT_EQ(isect.point.z, -1.0f);
    EXPECT_FLOAT_EQ(isect.normal.x, 0.0f);
    EXPECT_FLOAT_EQ(isect.normal.y, 0.0f);
    EXPECT_FLOAT_EQ(isect.normal.z, -1.0f);
    EXPECT_TRUE(isect.frontFace);

    // Ray missing
    Ray r2(Vec3f(2, 0, -5), Vec3f(0, 0, 1));
    EXPECT_FALSE(sphere.intersect(r2, isect));
}

TEST(SphereTest, Bounds) {
    auto mat = std::make_shared<Lambertian>(Color3f(1.0f));
    Sphere sphere(Vec3f(1, 2, 3), 1.5f, mat.get());
    
    AABB b = sphere.bounds();
    EXPECT_FLOAT_EQ(b.pMin.x, -0.5f);
    EXPECT_FLOAT_EQ(b.pMax.x, 2.5f);
    EXPECT_FLOAT_EQ(b.pMin.y, 0.5f);
    EXPECT_FLOAT_EQ(b.pMax.y, 3.5f);
}

// geometry-9: teğet normale dik olmalı ve u (φ) arttıkça noktanın hareket yönünü göstermeli.
TEST(SphereTest, TangentIsPerpendicularAndFollowsU) {
    auto mat = std::make_shared<Lambertian>(Color3f(1.0f));
    Sphere sphere(Vec3f(0, 0, 0), 2.0f, mat.get());
    for (int i = 0; i < 64; ++i) {
        float a = 0.37f + 0.81f * static_cast<float>(i);
        Vec3f dir = Vec3f(std::cos(a) * std::sin(1.3f * a), std::cos(1.3f * a), std::sin(a) * std::sin(1.3f * a)).normalized();
        Ray r(dir * 5.0f, -dir);
        SurfaceInteraction isect;
        ASSERT_TRUE(sphere.intersect(r, isect));
        Vec3f n = isect.ng.normalized();
        EXPECT_NEAR(isect.tangent.dot(n), 0.0f, 1e-4f);

        // Sonlu fark: teğet yönünde biraz ilerleyen bir noktanın u'su artmalı.
        Vec3f p2 = (n + isect.tangent * 1e-3f).normalized() * 2.0f;
        Ray r2(p2 * 2.5f, -p2.normalized());
        SurfaceInteraction isect2;
        ASSERT_TRUE(sphere.intersect(r2, isect2));
        float du = isect2.uv.x - isect.uv.x;
        if (du > 0.5f) du -= 1.0f;
        if (du < -0.5f) du += 1.0f;
        EXPECT_GT(du, 0.0f);
    }
}
