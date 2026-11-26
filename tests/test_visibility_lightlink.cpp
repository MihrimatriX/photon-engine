// test_visibility_lightlink.cpp — Ray Visibility bayrakları, Light Linking (Include/Exclude,
// NEE + MIS tutarlılığı), Shadow Linking ve proje JSON geriye uyumlu gidiş-dönüş testleri.

#include <gtest/gtest.h>
#include "camera/perspective_camera.h"
#include "engine/renderer.h"
#include "engine/scene.h"
#include "geometry/mesh.h"
#include "geometry/mesh_instance.h"
#include "geometry/sphere.h"
#include "lights/area_light.h"
#include "lights/directional_light.h"
#include "lights/point_light.h"
#include "materials/lambertian.h"
#include "materials/mirror.h"
#include "scene/document.h"
#include "scene/primitives.h"
#include "scene/project_io.h"
#include "scene/scene_graph.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <memory>
#include <vector>

using namespace photon;

namespace {

std::shared_ptr<TriangleMesh> makeQuadMesh(const Vec3f& p0, const Vec3f& p1, const Vec3f& p2, const Vec3f& p3,
                                           const Material* mat) {
    std::vector<Vec3f> pos = {p0, p1, p2, p3};
    std::vector<Vec3f> nrm;
    std::vector<Vec2f> uv = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};
    return std::make_shared<TriangleMesh>(pos, nrm, uv, idx, mat);
}

Color3f avgRegion(const Image& img, int x0, int y0, int x1, int y1) {
    Color3f sum(0.0f);
    int n = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            sum += img.getPixel(x, y);
            ++n;
        }
    }
    return n > 0 ? sum / static_cast<float>(n) : Color3f::black();
}

} // namespace

TEST(RayVisibility, CameraInvisibleStillCastsShadowAndReflection) {
    for (bool ownBvh : {false, true}) {
        Scene::setPreferOwnBvh(ownBvh);
        Scene scene;

        auto floorMat = std::make_shared<Lambertian>(Color3f(0.8f));
        auto blockerMat = std::make_shared<Lambertian>(Color3f(0.9f, 0.1f, 0.1f));
        auto mirrorMat = std::make_shared<Mirror>(Color3f(1.0f));
        scene.retainMaterial(floorMat);
        scene.retainMaterial(blockerMat);
        scene.retainMaterial(mirrorMat);

        // Zemin (y = 0)
        auto floor = makeQuadMesh({-4, 0, -4}, {4, 0, -4}, {4, 0, 4}, {-4, 0, 4}, floorMat.get());
        scene.addShape(floor);

        // Kameraya görünmez ama gölge ve parlak yansıma veren kırmızı küre: (0, 1, 0)
        auto sphere = std::make_shared<Sphere>(Vec3f(0.0f, 1.0f, 0.0f), 0.45f, blockerMat.get());
        sphere->setVisibilityMask(static_cast<uint8_t>(VisAll & ~VisCamera));
        scene.addShape(sphere);

        // Arkada ayna duvar (z = -2): kürenin yansımasını göstermeli
        auto mirror = makeQuadMesh({-1.5f, 0.2f, -2.0f}, {1.5f, 0.2f, -2.0f},
                                   {1.5f, 2.2f, -2.0f}, {-1.5f, 2.2f, -2.0f}, mirrorMat.get());
        scene.addShape(mirror);

        // Üstten yönlü ışık
        scene.addLight(std::make_shared<DirectionalLight>(Vec3f(0.0f, -1.0f, 0.0f), Color3f(3.0f)));
        scene.buildAccelerator();

        // Kamera küreye düz bakıyor: küreyi görmemeli, arkasındaki aynada kürenin kırmızı yansımasını görmeli!
        PerspectiveCamera cam(Vec3f(0.0f, 1.0f, 3.0f), Vec3f(0.0f, 1.0f, 0.0f),
                              Vec3f(0.0f, 1.0f, 0.0f), 45.0f, 1.0f);
        RenderSettings rs;
        rs.width = 48;
        rs.height = 48;
        rs.samplesPerPixel = 4;
        rs.maxBounces = 4;
        rs.numThreads = 1;
        rs.filter.type = PixelFilterType::Box;
        rs.filter.width = 1.0f;

        Renderer renderer;
        Image img = renderer.render(scene, cam, rs);

        // Merkez piksel aynadan yansıyan kırmızı küreyi görmeli (VisGlossy açık)
        const Color3f center = avgRegion(img, 21, 21, 27, 27);
        EXPECT_GT(center.r, 0.25f) << "ownBvh=" << ownBvh;
        EXPECT_LT(center.g, center.r * 0.4f) << "ownBvh=" << ownBvh;

        // Birincil ışın testi: küre doğrudan kameradan vurulmamalı (aynaya çarpmalı, t ≈ 5)
        Ray camRay(Vec3f(0.0f, 1.0f, 3.0f), Vec3f(0.0f, 0.0f, -1.0f));
        camRay.visibilityMask = VisCamera;
        SurfaceInteraction si;
        ASSERT_TRUE(scene.intersect(camRay, si));
        EXPECT_NEAR(si.t, 5.0f, 0.05f) << "Küre VisCamera kapalıyken kamera ışınını kesmemeli";

        // Gölge ışını testi: zeminin tam altından yukarı → küre gölge düşürmeli (VisShadow açık)
        Ray shadowRay(Vec3f(0.0f, 0.01f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f));
        shadowRay.visibilityMask = VisShadow;
        EXPECT_TRUE(scene.intersectAny(shadowRay)) << "VisShadow açıkken küre gölge düşürmeli";
    }
    Scene::setPreferOwnBvh(false);
}

TEST(RayVisibility, ShadowInvisibleDoesNotOcclude) {
    for (bool ownBvh : {false, true}) {
        Scene::setPreferOwnBvh(ownBvh);
        Scene scene;
        auto mat = std::make_shared<Lambertian>(Color3f(0.8f));
        scene.retainMaterial(mat);

        auto floor = makeQuadMesh({-3, 0, -3}, {3, 0, -3}, {3, 0, 3}, {-3, 0, 3}, mat.get());
        auto occluder = makeQuadMesh({-1, 1, -1}, {1, 1, -1}, {1, 1, 1}, {-1, 1, 1}, mat.get());
        occluder->setVisibilityMask(static_cast<uint8_t>(VisAll & ~VisShadow));
        scene.addShape(floor);
        scene.addShape(occluder);
        scene.buildAccelerator();

        Ray camRay(Vec3f(0.0f, 2.0f, 0.0f), Vec3f(0.0f, -1.0f, 0.0f));
        camRay.visibilityMask = VisCamera;
        SurfaceInteraction si;
        ASSERT_TRUE(scene.intersect(camRay, si));
        EXPECT_NEAR(si.t, 1.0f, 1e-3f);

        Ray shadowRay(Vec3f(0.0f, 0.01f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f));
        shadowRay.visibilityMask = VisShadow;
        EXPECT_FALSE(scene.intersectAny(shadowRay)) << "VisShadow kapalı engelleyici gölge kesmemeli";
    }
    Scene::setPreferOwnBvh(false);
}

TEST(LightLinking, IncludeAndExcludeModesWithMisParity) {
    Scene scene;
    auto whiteMat = std::make_shared<Lambertian>(Color3f(0.9f));
    scene.retainMaterial(whiteMat);

    // İki ayrı küre: solda A (uid=101), sağda B (uid=102)
    auto sphereA = std::make_shared<Sphere>(Vec3f(-1.2f, 0.0f, 0.0f), 0.6f, whiteMat.get());
    auto sphereB = std::make_shared<Sphere>(Vec3f( 1.2f, 0.0f, 0.0f), 0.6f, whiteMat.get());
    scene.tagShape(sphereA.get(), 101);
    scene.tagShape(sphereB.get(), 102);
    scene.addShape(sphereA);
    scene.addShape(sphereB);

    // Kırmızı alan ışığı (x ∈ [-2.2, -0.2], z ∈ [0.2, 1.6]): YALNIZ A'yı aydınlatır (Include {101})
    auto redLight = std::make_shared<AreaLight>(Vec3f(-2.2f, 2.4f, 0.2f), Vec3f(2.0f, 0.0f, 0.0f),
                                                Vec3f(0.0f, 0.0f, 1.4f), Color3f(12.0f, 0.0f, 0.0f));
    redLight->setVisibleToCamera(false);
    LightLinkSet redLink;
    redLink.mode = LightLinkSet::Mode::Include;
    redLink.nodeUids = {101};
    redLight->setLightLink(redLink);

    // Mavi alan ışığı (x ∈ [0.2, 2.2], z ∈ [0.2, 1.6]): A hariç hepsini aydınlatır (Exclude {101})
    auto blueLight = std::make_shared<AreaLight>(Vec3f(0.2f, 2.4f, 0.2f), Vec3f(2.0f, 0.0f, 0.0f),
                                                 Vec3f(0.0f, 0.0f, 1.4f), Color3f(0.0f, 0.0f, 12.0f));
    blueLight->setVisibleToCamera(false);
    LightLinkSet blueLink;
    blueLink.mode = LightLinkSet::Mode::Exclude;
    blueLink.nodeUids = {101};
    blueLight->setLightLink(blueLink);

    scene.addLight(redLight);
    scene.addLight(blueLight);
    scene.buildAccelerator();

    PerspectiveCamera cam(Vec3f(0.0f, 0.5f, 4.5f), Vec3f(0.0f, 0.0f, 0.0f),
                          Vec3f(0.0f, 1.0f, 0.0f), 45.0f, 1.0f);
    RenderSettings rs;
    rs.width = 64;
    rs.height = 64;
    rs.samplesPerPixel = 16;
    rs.maxBounces = 1;
    rs.numThreads = 1;
    rs.filter.type = PixelFilterType::Box;
    rs.filter.width = 1.0f;

    Renderer renderer;
    Image img = renderer.render(scene, cam, rs);

    // Sol küre (x ≈ 16, y ≈ 32): yalnız kırmızı, sıfır mavi!
    const Color3f leftCol = avgRegion(img, 14, 28, 22, 36);
    // Sağ küre (x ≈ 48, y ≈ 32): yalnız mavi, sıfır kırmızı!
    const Color3f rightCol = avgRegion(img, 42, 28, 50, 36);

    EXPECT_GT(leftCol.r, 0.2f);
    EXPECT_NEAR(leftCol.b, 0.0f, 1e-4f);
    EXPECT_GT(rightCol.b, 0.2f);
    EXPECT_NEAR(rightCol.r, 0.0f, 1e-4f);
}

TEST(LightLinking, ShadowLinkingSkipsExcludedBlocker) {
    auto renderCase = [](bool excludeBlocker) {
        Scene scene;
        auto mat = std::make_shared<Lambertian>(Color3f(0.8f));
        scene.retainMaterial(mat);

        auto receiver = makeQuadMesh({-2, 0, -2}, {2, 0, -2}, {2, 0, 2}, {-2, 0, 2}, mat.get());
        auto blocker = makeQuadMesh({-1.5f, 1.0f, -1.5f}, {1.5f, 1.0f, -1.5f},
                                    {1.5f, 1.0f, 1.5f}, {-1.5f, 1.0f, 1.5f}, mat.get());
        blocker->setVisibilityMask(static_cast<uint8_t>(VisAll & ~VisCamera));
        scene.tagShape(receiver.get(), 201);
        scene.tagShape(blocker.get(), 202);
        scene.addShape(receiver);
        scene.addShape(blocker);

        auto light = std::make_shared<PointLight>(Vec3f(0.0f, 3.0f, 0.0f), Color3f(40.0f));
        if (excludeBlocker) {
            LightLinkSet ll;
            ll.shadowMode = LightLinkSet::Mode::Exclude;
            ll.shadowNodeUids = {202};
            light->setLightLink(ll);
        }
        scene.addLight(light);
        scene.buildAccelerator();

        PerspectiveCamera cam(Vec3f(0.0f, 4.0f, 0.01f), Vec3f(0.0f, 0.0f, 0.0f),
                              Vec3f(0.0f, 0.0f, -1.0f), 45.0f, 1.0f);
        RenderSettings rs;
        rs.width = 24;
        rs.height = 24;
        rs.samplesPerPixel = 4;
        rs.maxBounces = 1;
        rs.numThreads = 1;
        rs.filter.type = PixelFilterType::Box;
        rs.filter.width = 1.0f;

        Renderer renderer;
        return avgRegion(renderer.render(scene, cam, rs), 8, 8, 16, 16).luminance();
    };

    const float shadowed = renderCase(false);
    const float unshadowed = renderCase(true);
    EXPECT_NEAR(shadowed, 0.0f, 1e-4f);
    EXPECT_GT(unshadowed, 0.3f);
}

TEST(LightLinking, ProjectJsonRoundTripPreservesVisibilityAndLinks) {
    SceneGraph graph;
    auto mat = std::make_shared<Lambertian>(Color3f(0.7f));
    auto nA = std::make_unique<SceneNode>("KureA", SceneNodeType::Sphere);
    nA->sphereRadius = 0.5f;
    nA->material = mat;
    nA->visibilityMask = static_cast<uint8_t>(VisCamera | VisGlossy);
    nA->transformTrack.setKey(TransformKey{0.0f, Vec3f(0, 0, 0), Vec3f(0, 0, 0), Vec3f(1, 1, 1)});
    nA->transformTrack.setKey(TransformKey{1.0f, Vec3f(2, 0, 0), Vec3f(0, 90, 0), Vec3f(1, 1, 1)});
    const uint64_t origUidA = nA->uid;

    auto nB = std::make_unique<SceneNode>("KureB", SceneNodeType::Sphere);
    nB->sphereRadius = 0.5f;
    nB->material = mat; // varsayılan visibilityMask = VisAll, transformTrack boş

    graph.root()->addChild(std::move(nA));
    graph.root()->addChild(std::move(nB));

    ProjectData data;
    LightDesc l;
    l.name = "Key";
    l.lightGroup = "KeyLight";
    l.lightLink.mode = LightLinkSet::Mode::Include;
    l.lightLink.nodeUids = {origUidA};
    l.lightLink.shadowMode = LightLinkSet::Mode::Exclude;
    l.lightLink.shadowNodeUids = {origUidA};
    data.lights.push_back(l);

    const std::string text = projectJsonText(graph, data, ".");
    nlohmann::json parsed = nlohmann::json::parse(text);
    // Geriye uyumluluk: varsayılan KureB yeni alanları yazmamalı
    ASSERT_EQ(parsed["nodes"].size(), 2u);
    EXPECT_TRUE(parsed["nodes"][0].contains("visibility"));
    EXPECT_TRUE(parsed["nodes"][0].contains("transformKeys"));
    EXPECT_FALSE(parsed["nodes"][1].contains("visibility"));
    EXPECT_FALSE(parsed["nodes"][1].contains("transformKeys"));

    SceneGraph loadedGraph;
    ProjectData loadedData;
    std::string err;
    ASSERT_TRUE(loadProjectJson(text, ".", loadedGraph, loadedData, &err)) << err;
    ASSERT_EQ(loadedGraph.root()->children.size(), 2u);
    const SceneNode* loadedA = loadedGraph.root()->children[0].get();
    const SceneNode* loadedB = loadedGraph.root()->children[1].get();

    EXPECT_EQ(loadedA->visibilityMask, static_cast<uint8_t>(VisCamera | VisGlossy));
    EXPECT_EQ(loadedB->visibilityMask, VisAll);
    EXPECT_EQ(loadedA->transformTrack.keys.size(), 2u);
    EXPECT_TRUE(loadedB->transformTrack.empty());

    ASSERT_EQ(loadedData.lights.size(), 1u);
    EXPECT_EQ(loadedData.lights[0].lightGroup, "KeyLight");
    EXPECT_EQ(loadedData.lights[0].lightLink.mode, LightLinkSet::Mode::Include);
    ASSERT_EQ(loadedData.lights[0].lightLink.nodeUids.size(), 1u);
    EXPECT_EQ(loadedData.lights[0].lightLink.nodeUids[0], loadedA->uid);
    EXPECT_EQ(loadedData.lights[0].lightLink.shadowMode, LightLinkSet::Mode::Exclude);
    ASSERT_EQ(loadedData.lights[0].lightLink.shadowNodeUids.size(), 1u);
    EXPECT_EQ(loadedData.lights[0].lightLink.shadowNodeUids[0], loadedA->uid);
}
