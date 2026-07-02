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
