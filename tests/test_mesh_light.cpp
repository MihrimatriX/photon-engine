#include <gtest/gtest.h>
#include "engine/scene.h"
#include "geometry/mesh.h"
#include "integrators/path_tracer.h"
#include "materials/lambertian.h"
#include "samplers/independent_sampler.h"
#include <memory>
#include <vector>

using namespace photon;

TEST(MeshLight, EmissiveQuadLightsDiffusePoint) {
    auto emitter = std::make_shared<Lambertian>(Color3f(0.0f), Color3f(8.0f));
    std::vector<Vec3f> lightPos = {
        {-0.5f, 2.0f, -0.5f}, {0.5f, 2.0f, -0.5f}, {0.5f, 2.0f, 0.5f}, {-0.5f, 2.0f, 0.5f}};
    std::vector<uint32_t> lightIdx = {0, 1, 2, 0, 2, 3};
    auto lightMesh = std::make_shared<TriangleMesh>(
        lightPos, std::vector<Vec3f>{}, std::vector<Vec2f>{}, lightIdx, emitter.get());

    auto floorMat = std::make_shared<Lambertian>(Color3f(0.8f));
    std::vector<Vec3f> floorPos = {
        {-1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 1.0f}};
    std::vector<Vec3f> floorN(4, Vec3f(0, 1, 0));
    std::vector<uint32_t> floorIdx = {0, 1, 2, 0, 2, 3};
    auto floor = std::make_shared<TriangleMesh>(floorPos, floorN, std::vector<Vec2f>{}, floorIdx, floorMat.get());

    Scene scene;
    scene.addShape(lightMesh);
    scene.addShape(floor);
    scene.buildAccelerator();

    PathTracer tracer(2, 8);
    IndependentSampler sampler(1);
    Ray ray(Vec3f(0.0f, 1.0f, 0.0f), Vec3f(0.0f, -1.0f, 0.0f));
    Color3f L = tracer.Li(ray, scene, sampler);
    EXPECT_GT(L.r + L.g + L.b, 0.01f);
}
