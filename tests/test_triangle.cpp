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
    EXPECT_FLOAT_EQ(isect.point.y, -0.33333334f); // centroid y-value of the hit
    EXPECT_FLOAT_EQ(isect.point.z, 0.0f);
    EXPECT_FLOAT_EQ(isect.normal.z, -1.0f); // normals flipped since ray is hitting the front of normal (0,0,1)

    // Ray missing
    Ray r2(Vec3f(2, 2, -5), Vec3f(0, 0, 1));
    EXPECT_FALSE(tri.intersect(r2, isect));
}
