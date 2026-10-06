// test_bvh.cpp — BVH testleri: doğru nesneye çarpma / ıskalama ve mesh, tek tek üçgenler ile BVH
// sonuçlarının (t, normal) birebir aynı olması. Hızlandırma yapısı sonucu değiştirmemeli,
// yalnız hızlandırmalı.
#include "gtest/gtest.h"
#include "geometry/bvh.h"
#include "geometry/sphere.h"
#include "geometry/mesh.h"
#include "geometry/triangle.h"
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

TEST(BVHTest, MeshQuadMatchesPerTriangle) {
    auto mat = std::make_shared<Lambertian>(Color3f(0.5f));
    std::vector<Vec3f> pos = {{-0.5f, 0, -0.5f}, {0.5f, 0, -0.5f}, {0.5f, 0, 0.5f}, {-0.5f, 0, 0.5f}};
    std::vector<Vec3f> nrm(4, Vec3f(0, 1, 0));
    std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};
    auto mesh = std::make_shared<TriangleMesh>(pos, nrm, std::vector<Vec2f>{}, idx, mat.get());

    Ray ray(Vec3f(0.1f, 2.0f, 0.2f), Vec3f(0, -1, 0));
    SurfaceInteraction fromMesh;
    Ray meshRay = ray;
    ASSERT_TRUE(mesh->intersect(meshRay, fromMesh));

    SurfaceInteraction fromTris;
    bool triHit = false;
    for (size_t i = 0; i < mesh->numTriangles(); ++i) {
        auto tri = mesh->getTriangle(i);
        Ray triRay = ray;
        SurfaceInteraction hit;
        if (tri->intersect(triRay, hit) && (!triHit || hit.t < fromTris.t)) {
            fromTris = hit;
            triHit = true;
        }
    }
    ASSERT_TRUE(triHit);
    EXPECT_NEAR(fromMesh.t, fromTris.t, 1e-4f);
    EXPECT_NEAR(fromMesh.normal.x, fromTris.normal.x, 1e-4f);
    EXPECT_NEAR(fromMesh.normal.y, fromTris.normal.y, 1e-4f);
    EXPECT_NEAR(fromMesh.normal.z, fromTris.normal.z, 1e-4f);

    BVH bvh;
    std::vector<std::shared_ptr<Shape>> prims = {mesh};
    bvh.build(prims);
    SurfaceInteraction fromBvh;
    Ray bvhRay = ray;
    ASSERT_TRUE(bvh.intersect(bvhRay, fromBvh));
    EXPECT_NEAR(fromBvh.t, fromMesh.t, 1e-4f);
    EXPECT_NEAR(fromBvh.ng.x, fromMesh.ng.x, 1e-4f);
    EXPECT_NEAR(fromBvh.ng.y, fromMesh.ng.y, 1e-4f);
    EXPECT_NEAR(fromBvh.ng.z, fromMesh.ng.z, 1e-4f);
}
