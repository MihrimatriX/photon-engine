#include <gtest/gtest.h>
#include "scene/scene_graph.h"
#include "materials/disney.h"
#include "io/obj_loader.h"
#include "core/image/image.h"
#include <filesystem>
#include <fstream>

using namespace photon;

TEST(MeshBake, UvSamplesAlbedoCorner) {
    auto img = std::make_shared<Image>(2, 2);
    img->setPixel(0, 0, Color3f(1, 0, 0));
    img->setPixel(1, 0, Color3f(1, 0, 0));
    img->setPixel(0, 1, Color3f(1, 0, 0));
    img->setPixel(1, 1, Color3f(0, 1, 0));

    auto mat = std::make_shared<DisneyMaterial>(Color3f(1, 0, 0), 0.0f, 0.5f, 0.5f);
    mat->setAlbedoImage(img);

    std::vector<Vec3f> pos = {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}};
    std::vector<Vec3f> nrm(4, Vec3f(0, 1, 0));
    std::vector<Vec2f> uv = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};
    auto mesh = std::make_shared<TriangleMesh>(pos, nrm, uv, idx, mat.get());

    SceneGraph graph;
    auto node = std::make_unique<SceneNode>("quad", SceneNodeType::Mesh);
    node->mesh = mesh;
    node->material = mat;
    graph.root()->addChild(std::move(node));

    Scene scene;
    graph.compile(scene);
    Ray ray(Vec3f(0.72f, 2.0f, 0.70f), Vec3f(0, -1, 0));
    SurfaceInteraction isect;
    ASSERT_TRUE(scene.intersect(ray, isect));
    EXPECT_GT(isect.uv.x, 0.5f);
    EXPECT_GT(isect.uv.y, 0.5f);
    auto shaded = mat->resolve(isect);
    EXPECT_GT(shaded.baseColor.g, shaded.baseColor.r);
}

TEST(MeshBake, NormalsUseInverseTranspose) {
    std::vector<Vec3f> pos = {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}};
    Vec3f n = Vec3f(1, 1, 0).normalized();
    std::vector<Vec3f> nrm(4, n);
    std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};
    auto mesh = std::make_shared<TriangleMesh>(pos, nrm, std::vector<Vec2f>{}, idx, nullptr);

    SceneGraph graph;
    auto node = std::make_unique<SceneNode>("quad", SceneNodeType::Mesh);
    node->mesh = mesh;
    node->localTransform = Transform::scale(Vec3f(2.0f, 1.0f, 1.0f));
    graph.root()->addChild(std::move(node));

    Scene scene;
    graph.compile(scene);
    Ray ray(Vec3f(1.0f, 3.0f, 0.3f), Vec3f(0, -1, 0));
    SurfaceInteraction isect;
    ASSERT_TRUE(scene.intersect(ray, isect));
    ASSERT_GT(std::abs(isect.normal.x), 1e-3f);
    EXPECT_NEAR(isect.normal.y / isect.normal.x, 2.0f, 0.05f);
}

TEST(ObjLoader, FanTriangulatesQuad) {
    namespace fs = std::filesystem;
    fs::path path = fs::temp_directory_path() / "photon_fan_quad.obj";
    {
        std::ofstream out(path);
        out << "v 0 0 0\nv 1 0 0\nv 1 0 1\nv 0 0 1\nf 1 2 3 4\n";
    }
    auto loaded = ObjLoader::load(path.string(), nullptr);
    fs::remove(path);
    ASSERT_EQ(loaded.meshes.size(), 1u);
    EXPECT_EQ(loaded.meshes[0]->numTriangles(), 2u);
}
