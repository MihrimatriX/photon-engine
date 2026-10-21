// Üçgen ışın kesişimi testleri (Möller–Trumbore), küçük ölçekli üçgenler dahil.

#include "gtest/gtest.h"
#include "geometry/triangle.h"
#include "materials/lambertian.h"

using namespace photon;

TEST(TriangleTest, RayIntersection) {
    auto mat = std::make_shared<Lambertian>(Color3f(1.0f));
    
    // Triangle in the XY plane
    Vec3f v0(-1, -1, 0);
    Vec3f v1(1, -1, 0);
    Vec3f v2(0, 1, 0);
    
    Vec3f n(0, 0, 1);
    Vec2f uv(0, 0);

    Triangle tri(v0, v1, v2, n, n, n, uv, uv, uv, mat.get());

    // Ray hitting front
    Ray r(Vec3f(0, 0, -5), Vec3f(0, 0, 1));
    SurfaceInteraction isect;
    EXPECT_TRUE(tri.intersect(r, isect));
    EXPECT_FLOAT_EQ(isect.t, 5.0f);
    EXPECT_FLOAT_EQ(isect.point.x, 0.0f);
    EXPECT_FLOAT_EQ(isect.point.y, 0.0f); // ray through origin on triangle plane
    EXPECT_FLOAT_EQ(isect.point.z, 0.0f);
    EXPECT_FLOAT_EQ(isect.normal.z, -1.0f); // flipped vs outward (0,0,1) for front-facing ray

    // Ray missing
    Ray r2(Vec3f(2, 2, -5), Vec3f(0, 0, 1));
    EXPECT_FALSE(tri.intersect(r2, isect));
}

// geometry-1: 1e-4 ölçekli üçgen (kenar 0.1 mm) 60° gelişle vurulmalı. Mutlak 1e-6
// determinant eşiğiyle bu üçgen görünmezdi (det ≈ |e1||e2|·cos60° ≈ 5e-9).
TEST(TriangleTest, TinyTriangleIsHit) {
    auto mat = std::make_shared<Lambertian>(Color3f(1.0f));
    const float s = 1e-4f;
    Vec3f n(0, 0, 1);
    Vec2f uv(0, 0);
    Triangle tri(Vec3f(0, 0, 0), Vec3f(s, 0, 0), Vec3f(0, s, 0), n, n, n, uv, uv, uv, mat.get());

    Vec3f target(0.25f * s, 0.25f * s, 0.0f);
    Vec3f dir = Vec3f(std::sin(1.0471976f), 0.0f, -std::cos(1.0471976f)); // normalden 60°
    Ray r(target - dir * 1.0f, dir);
    SurfaceInteraction isect;
    ASSERT_TRUE(tri.intersect(r, isect));
    EXPECT_NEAR(isect.t, 1.0f, 1e-5f);
    EXPECT_NEAR(isect.point.x, target.x, 1e-6f);

    // Paralel ışın yine reddedilir.
    Ray parallel(Vec3f(-1.0f, 0.25f * s, 0.0f), Vec3f(1, 0, 0));
    EXPECT_FALSE(tri.intersect(parallel, isect));
}
