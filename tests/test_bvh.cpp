#include "gtest/gtest.h"
#include "geometry/bvh.h"
#include "geometry/sphere.h"
#include "materials/lambertian.h"
#include <vector>
#include <memory>

using namespace photon;

TEST(BVHTest, BuildAndIntersection) {
    auto mat = std::make_shared<Lambertian>(Color3f(1.0f));
    
    std::vector<std::shared_ptr<Shape>> primitives;
    primitives.push_back(std::make_shared<Sphere>(Vec3f(-2, 0, 0), 1.0f, mat.get()));
    primitives.push_back(std::make_shared<Sphere>(Vec3f(2, 0, 0), 1.0f, mat.get()));
    
    BVH bvh;
    bvh.build(primitives);

    // Ray hitting first sphere
    Ray r1(Vec3f(-2, 0, -5), Vec3f(0, 0, 1));
    SurfaceInteraction isect1;
    EXPECT_TRUE(bvh.intersect(r1, isect1));
    EXPECT_FLOAT_EQ(isect1.point.x, -2.0f);

    // Ray hitting second sphere
    Ray r2(Vec3f(2, 0, -5), Vec3f(0, 0, 1));
    SurfaceInteraction isect2;
    EXPECT_TRUE(bvh.intersect(r2, isect2));
    EXPECT_FLOAT_EQ(isect2.point.x, 2.0f);

    // Ray missing both
    Ray r3(Vec3f(0, 0, -5), Vec3f(0, 0, 1));
    SurfaceInteraction isect3;
    EXPECT_FALSE(bvh.intersect(r3, isect3));
}
