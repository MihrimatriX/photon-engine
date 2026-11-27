// test_gpu_v2.cpp — Milestone 4 (GPU Motoru v2) doğrulama testleri:
//   * VK_KHR_ray_query Donanım RT vs Vulkan 1.1 Compute SAH BVH2 vs CPU eşliği
//   * 4 aşamalı Wavefront GPU yürütmesi vs Megakernel vs CPU eşliği
//   * Eşzamanlı Hibrit GPU+CPU vs CPU eşliği
//   * GPU üzerinde Ray Visibility, Light/Shadow Linking, Motion Blur, Light Groups (LG_*) & Cryptomatte
//   * Artımlı güncelleme (F3.8): Materials / Lights / Transforms BLAS'ı yeniden kurmadan anında yansır
//   * Şekilli ışıklar (F2.17): disk, küre, tüp ve tek yüzlü dikdörtgen GPU'da CPU ile aynı
#include <gtest/gtest.h>

#include "camera/perspective_camera.h"
#include "core/image/cryptomatte.h"
#include "core/math/transform.h"
#include "engine/render_device.h"
#include "engine/scene.h"
#include "engine/vulkan_device.h"
#include "geometry/mesh.h"
#include "geometry/mesh_instance.h"
#include "geometry/sphere.h"
#include "lights/area_light.h"
#include "lights/environment_light.h"
#include "lights/shape_lights.h"
#include "materials/disney.h"
#include "materials/lambertian.h"
#include "materials/mirror.h"
#include "scene/primitives.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace photon;

namespace {

bool hasVulkanDevice() {
    for (const DeviceInfo& d : enumerateDevices())
        if (d.usable && d.kind == DeviceKind::Vulkan) return true;
    return false;
}

std::shared_ptr<TriangleMesh> makeQuad(const Vec3f& p0, const Vec3f& p1, const Vec3f& p2, const Vec3f& p3,
                                       const Material* mat) {
    std::vector<Vec3f> verts = {p0, p1, p2, p3};
    Vec3f n = (p1 - p0).cross(p2 - p0).normalized();
    std::vector<Vec3f> norms = {n, n, n, n};
    std::vector<Vec2f> uvs = {Vec2f(0, 0), Vec2f(1, 0), Vec2f(1, 1), Vec2f(0, 1)};
    std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};
    return std::make_shared<TriangleMesh>(std::move(verts), std::move(norms), std::move(uvs), std::move(idx), mat);
}

std::shared_ptr<Scene> buildParityScene() {
    auto scene = std::make_shared<Scene>();
    auto floorMat = std::make_shared<Lambertian>(Color3f(0.65f, 0.65f, 0.65f));
    auto boxMat = std::make_shared<DisneyMaterial>(Color3f(0.8f, 0.25f, 0.15f), 0.1f, 0.35f, 0.5f);
    auto metalMat = std::make_shared<DisneyMaterial>(Color3f(0.85f, 0.72f, 0.35f), 0.9f, 0.25f, 0.5f);
    scene->retainMaterial(floorMat);
    scene->retainMaterial(boxMat);
    scene->retainMaterial(metalMat);
    scene->setMaterialName(floorMat.get(), "FloorClay");
    scene->setMaterialName(boxMat.get(), "HeroPaint");
    scene->setMaterialName(metalMat.get(), "GoldMetal");

    auto floor = makeQuad(Vec3f(-4, 0, -4), Vec3f(-4, 0, 4), Vec3f(4, 0, 4), Vec3f(4, 0, -4), floorMat.get());
    scene->setShapeName(floor.get(), "Floor");
    scene->addShape(floor);

    auto cubeMesh = makePrimitiveMesh(PrimitiveKind::Cube);
    auto cubeInst = std::make_shared<MeshInstance>(
        cubeMesh, Transform::translate(Vec3f(-0.75f, 0.0f, 0.0f)), boxMat.get());
    scene->setShapeName(cubeInst.get(), "HeroCube");
    scene->tagShape(cubeInst.get(), 101);
    scene->addShape(cubeInst);

    auto sphere = std::make_shared<Sphere>(Vec3f(0.8f, 0.55f, 0.1f), 0.55f, metalMat.get());
    scene->setShapeName(sphere.get(), "GoldSphere");
    scene->tagShape(sphere.get(), 102);
    scene->addShape(sphere);

    auto key = std::make_shared<AreaLight>(
        Vec3f(-1.5f, 2.8f, 1.2f), Vec3f(1.4f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 1.4f),
        Color3f(12.0f, 11.0f, 10.0f));
    key->setLightGroup("Key");
    scene->addLight(key);

    auto env = std::make_shared<EnvironmentLight>(nullptr, 0.0f, 0.25f);
    env->setLightGroup("Env");
    scene->setEnvironment(env);
    scene->buildAccelerator();
    return scene;
}

Image renderOn(RenderDevice& dev, const Camera& cam, const RenderSettings& rs,
               AovFilms* aovs = nullptr, RenderPassFilms* passes = nullptr) {
    Film film(rs.width, rs.height);
    if (aovs) aovs->resize(rs.width, rs.height);
    if (passes) passes->resize(rs.width, rs.height);
    RenderTargets t{&film, aovs, passes};
    const SampleBatchResult r = dev.render(cam, rs, t, 0, rs.samplesPerPixel, nullptr);
    EXPECT_TRUE(r.completed);
    EXPECT_EQ(r.samplesTraced, static_cast<uint64_t>(rs.width) * rs.height * rs.samplesPerPixel);
    return film.resolve();
}

void verifyParity(const Image& a, const Image& b, double maxRelMeanDiff, double maxRelMse) {
    ASSERT_EQ(a.width(), b.width());
    ASSERT_EQ(a.height(), b.height());
    double sumA = 0.0, sumB = 0.0, sqDiff = 0.0, sqRef = 0.0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const Color3f pa = a.getPixel(x, y);
            const Color3f pb = b.getPixel(x, y);
            ASSERT_TRUE(pa.isFinite()) << "NaN/Inf in image A at " << x << "," << y;
            ASSERT_TRUE(pb.isFinite()) << "NaN/Inf in image B at " << x << "," << y;
            for (int c = 0; c < 3; ++c) {
                sumA += pa[c];
                sumB += pb[c];
                const double d = static_cast<double>(pa[c]) - static_cast<double>(pb[c]);
                sqDiff += d * d;
                sqRef += static_cast<double>(pb[c]) * static_cast<double>(pb[c]);
            }
        }
    }
    const double relMean = std::abs(sumA - sumB) / std::max(sumB, 1e-6);
    const double relMse = sqDiff / std::max(sqRef, 1e-6);
    EXPECT_LT(relMean, maxRelMeanDiff) << "sumA=" << sumA << " sumB=" << sumB;
    EXPECT_LT(relMse, maxRelMse) << "relMse=" << relMse;
}

} // namespace

TEST(VulkanV2, RayQueryHardwareRtMatchesComputeAndCpu) {
    if (!hasVulkanDevice()) GTEST_SKIP() << "Vulkan cihazı bulunamadı";

    auto scene = buildParityScene();
    PerspectiveCamera cam(Vec3f(0.0f, 1.4f, 3.6f), Vec3f(0.0f, 0.45f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f), 40.0f, 1.0f);
    RenderSettings rs;
    rs.width = 32;
    rs.height = 32;
    rs.samplesPerPixel = 32;
    rs.maxBounces = 4;
    rs.denoiseEnabled = false;
    rs.filter = PixelFilter{PixelFilterType::Box, 1.0f};

    auto cpu = createDevice("cpu");
    ASSERT_NE(cpu, nullptr);
    cpu->setScene(scene);
    Image imgCpu = renderOn(*cpu, cam, rs);

    std::string err;
    auto gpu = createDevice("vulkan:0", &err);
    ASSERT_NE(gpu, nullptr) << err;
    gpu->setScene(scene);

    rs.gpuMode = GpuMode::Megakernel;
    Image imgCompute = renderOn(*gpu, cam, rs);

    rs.gpuMode = GpuMode::RayQuery;
    Image imgRt = renderOn(*gpu, cam, rs);

    verifyParity(imgCompute, imgCpu, 0.08, 0.08);
    verifyParity(imgRt, imgCpu, 0.08, 0.08);
    verifyParity(imgRt, imgCompute, 0.05, 0.05);
}

TEST(VulkanV2, WavefrontMatchesMegakernelAndCpu) {
    if (!hasVulkanDevice()) GTEST_SKIP() << "Vulkan cihazı bulunamadı";

    auto scene = buildParityScene();
    PerspectiveCamera cam(Vec3f(0.0f, 1.4f, 3.6f), Vec3f(0.0f, 0.45f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f), 40.0f, 1.0f);
    RenderSettings rs;
    rs.width = 32;
    rs.height = 32;
    rs.samplesPerPixel = 24;
    rs.maxBounces = 4;
    rs.denoiseEnabled = false;
    rs.filter = PixelFilter{PixelFilterType::Box, 1.0f};

    auto cpu = createDevice("cpu");
    cpu->setScene(scene);
    Image imgCpu = renderOn(*cpu, cam, rs);

    auto gpu = createDevice("vulkan:0");
    ASSERT_NE(gpu, nullptr);
    gpu->setScene(scene);

    rs.gpuMode = GpuMode::Megakernel;
    Image imgMega = renderOn(*gpu, cam, rs);

    rs.gpuMode = GpuMode::Wavefront;
    Image imgWf = renderOn(*gpu, cam, rs);

    verifyParity(imgWf, imgCpu, 0.08, 0.08);
    verifyParity(imgWf, imgMega, 0.05, 0.05);
}

TEST(VulkanV2, HybridMatchesCpu) {
    if (!hasVulkanDevice()) GTEST_SKIP() << "Vulkan cihazı bulunamadı";

    auto scene = buildParityScene();
    PerspectiveCamera cam(Vec3f(0.0f, 1.4f, 3.6f), Vec3f(0.0f, 0.45f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f), 40.0f, 1.0f);
    RenderSettings rs;
    rs.width = 24;
    rs.height = 24;
    rs.samplesPerPixel = 24;
    rs.maxBounces = 4;
    rs.denoiseEnabled = false;
    rs.filter = PixelFilter{PixelFilterType::Box, 1.0f};

    auto cpu = createDevice("cpu");
    cpu->setScene(scene);
    Image imgCpu = renderOn(*cpu, cam, rs);

    std::string err;
    auto hybrid = createDevice("hybrid", &err);
    ASSERT_NE(hybrid, nullptr) << err;
    EXPECT_EQ(hybrid->info().kind, DeviceKind::Hybrid);
    hybrid->setScene(scene);

    AovFilms aovs;
    RenderPassFilms passes;
    Image imgHyb = renderOn(*hybrid, cam, rs, &aovs, &passes);

    verifyParity(imgHyb, imgCpu, 0.08, 0.08);
    EXPECT_EQ(passes.lightGroups.size(), 2u);
    EXPECT_FALSE(passes.cryptoObject.manifest().empty());
}

TEST(VulkanV2, GpuVisibilityLightLinkMotionBlurLightGroupsCryptoMatchCpu) {
    if (!hasVulkanDevice()) GTEST_SKIP() << "Vulkan cihazı bulunamadı";

    // 1. Light Linking + Ray Visibility + Light Groups + Cryptomatte GPU doğrulaması
    auto scene = std::make_shared<Scene>();
    auto whiteMat = std::make_shared<Lambertian>(Color3f(0.8f, 0.8f, 0.8f));
    auto mirrorMat = std::make_shared<Mirror>(Color3f(0.95f, 0.95f, 0.95f));
    scene->retainMaterial(whiteMat);
    scene->retainMaterial(mirrorMat);
    scene->setMaterialName(whiteMat.get(), "WhiteClay");
    scene->setMaterialName(mirrorMat.get(), "ChromeMirror");

    auto leftSphere = std::make_shared<Sphere>(Vec3f(-1.0f, 0.0f, 0.0f), 0.6f, whiteMat.get());
    auto rightSphere = std::make_shared<Sphere>(Vec3f(1.0f, 0.0f, 0.0f), 0.6f, whiteMat.get());
    // Kameraya görünmez (yalnız yansımada) orta kutu
    auto ghostCubeMesh = makePrimitiveMesh(PrimitiveKind::Cube);
    auto ghostCube = std::make_shared<MeshInstance>(
        ghostCubeMesh, Transform::translate(Vec3f(0.0f, -0.175f, 0.5f)) * Transform::scale(Vec3f(0.35f)), whiteMat.get());
    ghostCube->setVisibilityMask(VisAll & ~VisCamera);

    scene->tagShape(leftSphere.get(), 11);
    scene->setShapeName(leftSphere.get(), "LeftSphere");
    scene->tagShape(rightSphere.get(), 22);
    scene->setShapeName(rightSphere.get(), "RightSphere");
    scene->tagShape(ghostCube.get(), 33);
    scene->setShapeName(ghostCube.get(), "GhostCube");
    scene->addShape(leftSphere);
    scene->addShape(rightSphere);
    scene->addShape(ghostCube);

    // Kırmızı ışık yalnız LeftSphere'e (uid=11) bağlı, Key grubunda
    auto redLight = std::make_shared<AreaLight>(
        Vec3f(-1.6f, 2.0f, 1.5f), Vec3f(1.2f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 1.2f),
        Color3f(25.0f, 0.0f, 0.0f));
    redLight->setVisibleToCamera(false);
    redLight->setLightGroup("Key");
    LightLinkSet llRed;
    llRed.mode = LightLinkSet::Mode::Include;
    llRed.nodeUids = {11};
    redLight->setLightLink(llRed);
    scene->addLight(redLight);

    // Mavi ışık yalnız RightSphere'e (uid=22) bağlı, Rim grubunda
    auto blueLight = std::make_shared<AreaLight>(
        Vec3f(0.4f, 2.0f, 1.5f), Vec3f(1.2f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 1.2f),
        Color3f(0.0f, 0.0f, 25.0f));
    blueLight->setVisibleToCamera(false);
    blueLight->setLightGroup("Rim");
    LightLinkSet llBlue;
    llBlue.mode = LightLinkSet::Mode::Include;
    llBlue.nodeUids = {22};
    blueLight->setLightLink(llBlue);
    scene->addLight(blueLight);
    scene->buildAccelerator();

    PerspectiveCamera cam(Vec3f(0.0f, 0.0f, 4.5f), Vec3f(0.0f, 0.0f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f), 40.0f, 1.0f);
    RenderSettings rs;
    rs.width = 32;
    rs.height = 32;
    rs.samplesPerPixel = 24;
    rs.maxBounces = 3;
    rs.denoiseEnabled = false;
    rs.gpuMode = GpuMode::RayQuery;

    auto gpu = createDevice("vulkan:0");
    ASSERT_NE(gpu, nullptr);
    gpu->setScene(scene);

    AovFilms aovs;
    RenderPassFilms passes;
    Image beauty = renderOn(*gpu, cam, rs, &aovs, &passes);

    // GhostCube (merkez x=16, y=16) kameraya görünmez olmalı → siyah / alpha 0
    EXPECT_LT(beauty.getPixel(16, 16).luminance(), 1e-4f);

    // Sol küre (x=7, y=16): yalnız kırmızı ışık (Key)
    const Color3f leftPx = beauty.getPixel(7, 16);
    EXPECT_GT(leftPx.r, 0.1f);
    EXPECT_LT(leftPx.b, 0.01f);

    // Sağ küre (x=24, y=16): yalnız mavi ışık (Rim)
    const Color3f rightPx = beauty.getPixel(24, 16);
    EXPECT_GT(rightPx.b, 0.1f);
    EXPECT_LT(rightPx.r, 0.01f);

    // GPU Light Groups: Σ LG_g == Beauty
    ASSERT_EQ(passes.lightGroups.size(), 2u);
    Image lg0 = passes.lightGroups[0].second.resolve();
    Image lg1 = passes.lightGroups[1].second.resolve();
    float maxLgDiff = 0.0f;
    for (int y = 0; y < rs.height; ++y)
        for (int x = 0; x < rs.width; ++x) {
            const Color3f b = beauty.getPixel(x, y);
            const Color3f s = lg0.getPixel(x, y) + lg1.getPixel(x, y);
            maxLgDiff = std::max({maxLgDiff, std::abs(b.r - s.r), std::abs(b.g - s.g), std::abs(b.b - s.b)});
        }
    EXPECT_LT(maxLgDiff, 1e-3f);

    // GPU Cryptomatte: LeftSphere ve RightSphere kimlikleri doğru ve manifest dolu
    const auto leftRanks = passes.cryptoObject.pixelRanks(7, 16);
    ASSERT_FALSE(leftRanks.empty());
    EXPECT_FLOAT_EQ(leftRanks[0].first, cryptoNameToFloat("LeftSphere"));
    EXPECT_NEAR(leftRanks[0].second, 1.0f, 1e-3f);
    const auto rightRanks = passes.cryptoObject.pixelRanks(24, 16);
    ASSERT_FALSE(rightRanks.empty());
    EXPECT_FLOAT_EQ(rightRanks[0].first, cryptoNameToFloat("RightSphere"));
    EXPECT_NE(passes.cryptoObject.manifestJson().find("\"LeftSphere\""), std::string::npos);

    // 2. GPU Nesne Hareket Bulanıklığı (Motion Blur)
    auto mbScene = std::make_shared<Scene>();
    auto emitMat = std::make_shared<Lambertian>(Color3f(0.0f), Color3f(2.0f, 2.0f, 2.0f));
    mbScene->retainMaterial(emitMat);
    auto cube = makePrimitiveMesh(PrimitiveKind::Cube);
    auto moving = std::make_shared<MeshInstance>(
        cube, Transform::translate(Vec3f(-1.2f, -0.2f, 0.0f)) * Transform::scale(Vec3f(0.4f)), emitMat.get());
    moving->setMotionTransform(Transform::translate(Vec3f(1.2f, -0.2f, 0.0f)) * Transform::scale(Vec3f(0.4f)));
    mbScene->addShape(moving);
    mbScene->buildAccelerator();

    RenderSettings mbRs = rs;
    mbRs.motionBlur = true;
    mbRs.samplesPerPixel = 64;
    mbRs.maxBounces = 1;
    mbRs.filter = PixelFilter{PixelFilterType::Box, 1.0f};

    gpu->setScene(mbScene);
    Image mbGpu = renderOn(*gpu, cam, mbRs);

    auto cpu = createDevice("cpu");
    cpu->setScene(mbScene);
    Image mbCpu = renderOn(*cpu, cam, mbRs);

    // Merkezde (x=16, y=16) yatay hareket izi görünmeli ve GPU-CPU uyumlu olmalı
    EXPECT_GT(mbGpu.getPixel(16, 16).luminance(), 0.1f);
    verifyParity(mbGpu, mbCpu, 0.10, 0.10);
}

TEST(VulkanV2, IncrementalMaterialAndLightUpdate) {
    if (!hasVulkanDevice()) GTEST_SKIP() << "Vulkan cihazı bulunamadı";

    auto scene = std::make_shared<Scene>();
    auto mat = std::make_shared<DisneyMaterial>(Color3f(0.9f, 0.05f, 0.05f), 0.0f, 0.5f, 0.5f);
    scene->retainMaterial(mat);
    auto cubeMesh = makePrimitiveMesh(PrimitiveKind::Cube);
    auto inst = std::make_shared<MeshInstance>(
        cubeMesh, Transform::translate(Vec3f(0.0f, -0.6f, 0.0f)) * Transform::scale(Vec3f(1.2f)), mat.get());
    scene->addShape(inst);

    auto light = std::make_shared<AreaLight>(
        Vec3f(-1.0f, 2.5f, 2.0f), Vec3f(2.0f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 1.5f),
        Color3f(10.0f, 10.0f, 10.0f));
    light->setVisibleToCamera(false);
    scene->addLight(light);
    scene->buildAccelerator();

    std::string err;
    auto dev = createDevice("vulkan:0", &err);
    ASSERT_NE(dev, nullptr) << err;
    auto* vkDev = dynamic_cast<VulkanDevice*>(dev.get());
    ASSERT_NE(vkDev, nullptr);

    PerspectiveCamera cam(Vec3f(0.0f, 0.5f, 3.5f), Vec3f(0.0f, 0.0f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f), 40.0f, 1.0f);
    RenderSettings rs;
    rs.width = 16;
    rs.height = 16;
    rs.samplesPerPixel = 16;
    rs.maxBounces = 2;
    rs.denoiseEnabled = false;

    vkDev->setScene(scene);
    const VulkanUploadStats s0 = vkDev->uploadStats();
    Image imgRed = renderOn(*vkDev, cam, rs);
    const Color3f cRed = imgRed.getPixel(8, 8);
    EXPECT_GT(cRed.r, cRed.g * 3.0f);

    // 1. Artımlı malzeme güncellemesi: BLAS/TLAS yeniden kurulmadan kırmızı → yeşil
    mat->setBaseColor(Color3f(0.05f, 0.9f, 0.05f));
    vkDev->updateScene(scene, SceneUpdateKind::Materials);
    const VulkanUploadStats s1 = vkDev->uploadStats();
    EXPECT_EQ(s1.materialUpdates, s0.materialUpdates + 1);
    EXPECT_EQ(s1.blasBuilds, s0.blasBuilds);
    EXPECT_EQ(s1.tlasBuilds, s0.tlasBuilds);

    Image imgGreen = renderOn(*vkDev, cam, rs);
    const Color3f cGreen = imgGreen.getPixel(8, 8);
    EXPECT_GT(cGreen.g, cGreen.r * 3.0f);

    // 2. Artımlı ışık güncellemesi: parlaklık 4× artar, BLAS/TLAS değişmez
    light->setRadiance(Color3f(40.0f, 40.0f, 40.0f));
    vkDev->updateScene(scene, SceneUpdateKind::Lights);
    const VulkanUploadStats s2 = vkDev->uploadStats();
    EXPECT_EQ(s2.lightUpdates, s1.lightUpdates + 1);
    EXPECT_EQ(s2.blasBuilds, s0.blasBuilds);
    EXPECT_EQ(s2.tlasBuilds, s0.tlasBuilds);

    Image imgBright = renderOn(*vkDev, cam, rs);
    EXPECT_GT(imgBright.getPixel(8, 8).g, cGreen.g * 2.5f);

    // 3. Artımlı dönüşüm güncellemesi: önbellekli BLAS korunur, yalnız TLAS yeniden kurulur
    vkDev->updateScene(scene, SceneUpdateKind::Transforms);
    const VulkanUploadStats s3 = vkDev->uploadStats();
    EXPECT_EQ(s3.transformUpdates, s2.transformUpdates + 1);
    EXPECT_EQ(s3.blasBuilds, s0.blasBuilds);
    if (vkDev->hasRayQuery()) EXPECT_EQ(s3.tlasBuilds, s0.tlasBuilds + 1);
}

// Disk, küre ve tüp ışıkları ile tek yüzlü dikdörtgen: analitik şekillerin GPU kesişimi (ikincil
// BVH), yeni ışık türlerinin örneklemesi/pdf'i ve tek yüzlü yayım CPU ile aynı görüntüyü vermeli.
// Yukarı bakan tek yüzlü panel sahneyi aydınlatmamalı; kamera görünür ışıkları doğrudan görür.
TEST(VulkanV2, ShapeLightsAndOneSidedMatchCpu) {
    if (!hasVulkanDevice()) GTEST_SKIP() << "Vulkan cihazı bulunamadı";

    auto scene = std::make_shared<Scene>();
    auto floorMat = std::make_shared<Lambertian>(Color3f(0.7f, 0.7f, 0.7f));
    auto ballMat = std::make_shared<DisneyMaterial>(Color3f(0.2f, 0.4f, 0.8f), 0.0f, 0.3f, 0.5f);
    scene->retainMaterial(floorMat);
    scene->retainMaterial(ballMat);
    scene->addShape(makeQuad(Vec3f(-4, 0, -4), Vec3f(-4, 0, 4), Vec3f(4, 0, 4), Vec3f(4, 0, -4), floorMat.get()));
    scene->addShape(std::make_shared<Sphere>(Vec3f(0.0f, 0.5f, 0.0f), 0.5f, ballMat.get()));

    // Aşağı bakan tek yüzlü elips disk (sol üst) ve aşağı bakan tek yüzlü dikdörtgen (sağ üst).
    auto disk = std::make_shared<DiskLight>(Vec3f(-1.2f, 2.2f, 0.5f), Vec3f(0.6f, 0.0f, 0.0f),
                                            Vec3f(0.0f, -0.15f, 0.35f), Color3f(9.0f, 8.0f, 7.0f), false);
    scene->addLight(disk);
    auto rect = std::make_shared<AreaLight>(Vec3f(0.8f, 2.4f, -0.4f), Vec3f(0.9f, 0.0f, 0.0f),
                                            Vec3f(0.0f, 0.0f, 0.9f), Color3f(6.0f, 6.0f, 8.0f), false);
    scene->addLight(rect);
    // Yukarı bakan tek yüzlü panel: sahneye hiç ışık vermez (iki yüzlü olsaydı zemini yakardı).
    auto away = std::make_shared<AreaLight>(Vec3f(-0.5f, 1.6f, 1.2f), Vec3f(0.0f, 0.0f, 0.8f),
                                            Vec3f(1.0f, 0.0f, 0.0f), Color3f(40.0f), false);
    ASSERT_GT(away->normal().y, 0.99f);
    scene->addLight(away);
    // Kameranın gördüğü küre ve tüp ışıkları.
    auto bulb = std::make_shared<SphereLight>(Vec3f(1.3f, 0.9f, 0.8f), 0.2f, Color3f(20.0f, 14.0f, 8.0f));
    bulb->setVisibleToCamera(true);
    scene->addLight(bulb);
    auto tube = std::make_shared<CylinderLight>(Vec3f(-0.4f, 1.4f, -1.0f), Vec3f(0.9f, 0.2f, 0.0f), 0.06f,
                                                Color3f(30.0f, 30.0f, 40.0f));
    tube->setVisibleToCamera(true);
    scene->addLight(tube);
    scene->buildAccelerator();

    PerspectiveCamera cam(Vec3f(0.0f, 1.6f, 4.0f), Vec3f(0.0f, 0.7f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f), 45.0f, 1.0f);
    RenderSettings rs;
    rs.width = 32;
    rs.height = 32;
    rs.samplesPerPixel = 64;
    rs.maxBounces = 3;
    rs.denoiseEnabled = false;
    rs.filter = PixelFilter{PixelFilterType::Box, 1.0f};

    auto cpu = createDevice("cpu");
    ASSERT_NE(cpu, nullptr);
    cpu->setScene(scene);
    Image imgCpu = renderOn(*cpu, cam, rs);

    std::string err;
    auto gpu = createDevice("vulkan:0", &err);
    ASSERT_NE(gpu, nullptr) << err;
    gpu->setScene(scene);

    rs.gpuMode = GpuMode::Megakernel;
    Image imgMega = renderOn(*gpu, cam, rs);
    verifyParity(imgMega, imgCpu, 0.06, 0.08);

    rs.gpuMode = GpuMode::Wavefront;
    Image imgWf = renderOn(*gpu, cam, rs);
    verifyParity(imgWf, imgCpu, 0.06, 0.08);

    auto* vkDev = dynamic_cast<VulkanDevice*>(gpu.get());
    if (vkDev && vkDev->hasRayQuery()) {
        rs.gpuMode = GpuMode::RayQuery;
        Image imgRt = renderOn(*gpu, cam, rs);
        verifyParity(imgRt, imgCpu, 0.06, 0.08);
    }
}
