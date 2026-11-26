// test_v04_production.cpp — Milestone 3 (v0.4 prodüksiyon katmanı) testleri:
//   * Light Groups (EXR LG_* katmanları, toplam = Beauty)
//   * Psyop Cryptomatte 1.2.0 (MurmurHash3, güvenli float32, rank sıralaması, EXR manifest)
//   * SD-Tree Path Guiding (DirectionalTree PDF tutarlılığı, yansızlık, dolaylı varyans düşüşü)
#include <gtest/gtest.h>

#include "camera/perspective_camera.h"
#include "core/image/cryptomatte.h"
#include "core/image/image_io.h"
#include "core/math/constants.h"
#include "engine/renderer.h"
#include "engine/scene.h"
#include "geometry/mesh.h"
#include "geometry/sphere.h"
#include "integrators/path_guiding.h"
#include "lights/area_light.h"
#include "lights/environment_light.h"
#include "materials/lambertian.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

using namespace photon;

namespace {

std::shared_ptr<TriangleMesh> makeQuadMesh(const Vec3f& p0, const Vec3f& p1, const Vec3f& p2, const Vec3f& p3,
                                           const Material* mat) {
    std::vector<Vec3f> verts = {p0, p1, p2, p3};
    Vec3f n = (p1 - p0).cross(p2 - p0).normalized();
    std::vector<Vec3f> norms = {n, n, n, n};
    std::vector<Vec2f> uvs = {Vec2f(0, 0), Vec2f(1, 0), Vec2f(1, 1), Vec2f(0, 1)};
    std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};
    return std::make_shared<TriangleMesh>(std::move(verts), std::move(norms), std::move(uvs), std::move(idx), mat);
}

bool hasChannel(const std::vector<std::string>& channels, const std::string& name) {
    return std::find(channels.begin(), channels.end(), name) != channels.end();
}

} // namespace

TEST(LightGroups, SumEqualsBeautyAndSeparatesLights) {
    Scene scene;
    auto whiteMat = std::make_shared<Lambertian>(Color3f(0.8f, 0.8f, 0.8f));
    scene.retainMaterial(whiteMat);
    scene.setMaterialName(whiteMat.get(), "WhiteClay");

    auto sphere = std::make_shared<Sphere>(Vec3f(0.0f, 0.0f, 0.0f), 1.0f, whiteMat.get());
    scene.setShapeName(sphere.get(), "CenterSphere");
    scene.addShape(sphere);

    // Kırmızı Key ışığı (sol ön üst, küreye bakan)
    auto key = std::make_shared<AreaLight>(
        Vec3f(-2.2f, 1.2f, 2.2f), Vec3f(1.2f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 1.2f),
        Color3f(30.0f, 0.0f, 0.0f));
    key->setLightGroup("Key");
    scene.addLight(key);

    // Mavi Rim ışığı (sağ ön üst, küreye bakan)
    auto rim = std::make_shared<AreaLight>(
        Vec3f(1.0f, 1.2f, 2.2f), Vec3f(1.2f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 1.2f),
        Color3f(0.0f, 0.0f, 30.0f));
    rim->setLightGroup("Rim");
    scene.addLight(rim);

    // Yeşil ortam ışığı (1x1 sabit HDR harita)
    auto envImg = std::make_shared<Image>(2, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            envImg->setPixel(x, y, Color3f(0.0f, 0.35f, 0.0f));
    auto env = std::make_shared<EnvironmentLight>(envImg, 0.0f, 1.0f);
    env->setLightGroup("Env");
    scene.setEnvironment(env);
    scene.buildAccelerator();

    ASSERT_EQ(scene.lightGroupNames().size(), 3u);

    PerspectiveCamera cam(Vec3f(0.0f, 0.0f, 4.0f), Vec3f(0.0f, 0.0f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f), 40.0f, 1.0f);
    RenderSettings rs;
    rs.width = 24;
    rs.height = 24;
    rs.samplesPerPixel = 16;
    rs.maxBounces = 3;
    rs.denoiseEnabled = false;
    rs.filter = PixelFilter{PixelFilterType::Box, 1.0f};

    Renderer renderer;
    RenderPassFilms passes;
    AovFilms aovs;
    Image beauty = renderer.render(scene, cam, rs, nullptr, &passes, &aovs);

    ASSERT_EQ(passes.lightGroups.size(), 3u);
    EXPECT_EQ(passes.lightGroups[0].first, "Key");
    EXPECT_EQ(passes.lightGroups[1].first, "Rim");
    EXPECT_EQ(passes.lightGroups[2].first, "Env");

    Image lgKey = passes.lightGroups[0].second.resolve();
    Image lgRim = passes.lightGroups[1].second.resolve();
    Image lgEnv = passes.lightGroups[2].second.resolve();

    // Tüm piksellerde Σ LG_g == Beauty (< 1e-4)
    float maxDiff = 0.0f;
    for (int y = 0; y < rs.height; ++y) {
        for (int x = 0; x < rs.width; ++x) {
            const Color3f b = beauty.getPixel(x, y);
            const Color3f s = lgKey.getPixel(x, y) + lgRim.getPixel(x, y) + lgEnv.getPixel(x, y);
            maxDiff = std::max({maxDiff, std::abs(b.r - s.r), std::abs(b.g - s.g), std::abs(b.b - s.b)});
        }
    }
    EXPECT_LT(maxDiff, 1e-4f);

    // Merkez pikselde kanallar tam ayrışmış olmalı
    const Color3f ck = lgKey.getPixel(12, 12);
    const Color3f cr = lgRim.getPixel(12, 12);
    const Color3f ce = lgEnv.getPixel(12, 12);
    EXPECT_GT(ck.r, 0.1f);
    EXPECT_LT(ck.g + ck.b, 1e-4f);
    EXPECT_GT(cr.b, 0.1f);
    EXPECT_LT(cr.r + cr.g, 1e-4f);
    EXPECT_GT(ce.g, 0.05f);
    EXPECT_LT(ce.r + ce.b, 1e-4f);

    // Çok katmanlı EXR çıktısında LG_* kanalları yer almalı
    const std::string exrPath = (std::filesystem::temp_directory_path() / "photon_test_light_groups.exr").string();
    std::vector<Image> storage;
    std::vector<CryptoRankImage> cryptoStorage;
    const auto layers = makeExrLayers(&passes, &aovs, storage, &cryptoStorage);
    const auto meta = makeExrCryptoMetadata(&passes);
    const Image alpha = aovs.alpha.resolve();
    ASSERT_TRUE(saveMultiLayerEXR(exrPath, beauty, &alpha, layers, meta));

    const auto channels = readExrChannelNames(exrPath);
    EXPECT_TRUE(hasChannel(channels, "LG_Key.R"));
    EXPECT_TRUE(hasChannel(channels, "LG_Rim.G"));
    EXPECT_TRUE(hasChannel(channels, "LG_Env.B"));
    std::filesystem::remove(exrPath);
}

TEST(Cryptomatte, MurmurHashFloatConversionRankAndExrRoundTrip) {
    // 1. Güvenli float32 dönüşümü: kritik 32-bit değerler asla NaN/Inf/denorm üretmemeli
    const uint32_t tricky[] = {0x00000000u, 0x00000001u, 0x007FFFFFu, 0x7F800000u,
                               0x7FC00000u, 0x7FFFFFFFu, 0x80000000u, 0xFF800000u, 0xFFFFFFFFu};
    for (uint32_t h : tricky) {
        const float f = cryptoHashToFloat(h);
        EXPECT_TRUE(std::isfinite(f));
        EXPECT_NE(std::fpclassify(f), FP_SUBNORMAL);
        EXPECT_NE(std::fpclassify(f), FP_ZERO);
    }
    EXPECT_EQ(cryptoHashName("Hero_A"), cryptoHashName("Hero_A"));
    EXPECT_NE(cryptoHashName("Hero_A"), cryptoHashName("Hero_B"));

    // 2. İki bitişik nesneli sahne: kenar piksellerinde anti-aliased çoklu rank
    Scene scene;
    auto matRed = std::make_shared<Lambertian>(Color3f(0.8f, 0.1f, 0.1f));
    auto matBlue = std::make_shared<Lambertian>(Color3f(0.1f, 0.2f, 0.8f));
    scene.retainMaterial(matRed);
    scene.retainMaterial(matBlue);
    scene.setMaterialName(matRed.get(), "Mat_Red");
    scene.setMaterialName(matBlue.get(), "Mat_Blue");

    auto leftBox = makeQuadMesh(Vec3f(-1.5f, -1.0f, 0.0f), Vec3f(0.0f, -1.0f, 0.0f),
                                Vec3f(0.0f, 1.0f, 0.0f), Vec3f(-1.5f, 1.0f, 0.0f), matRed.get());
    auto rightBox = makeQuadMesh(Vec3f(0.0f, -1.0f, 0.0f), Vec3f(1.5f, -1.0f, 0.0f),
                                 Vec3f(1.5f, 1.0f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f), matBlue.get());
    scene.setShapeName(leftBox.get(), "Hero_A");
    scene.setShapeName(rightBox.get(), "Hero_B");
    scene.addShape(leftBox);
    scene.addShape(rightBox);
    scene.setEnvironment(std::make_shared<EnvironmentLight>(nullptr, 0.0f, 1.0f));
    scene.buildAccelerator();

    PerspectiveCamera cam(Vec3f(0.0f, 0.0f, 4.0f), Vec3f(0.0f, 0.0f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f), 45.0f, 1.0f);
    RenderSettings rs;
    rs.width = 33; // tek genişlik → x = 16 pikseli tam x = 0 ekleminden geçer!
    rs.height = 33;
    rs.samplesPerPixel = 32;
    rs.maxBounces = 1;
    rs.denoiseEnabled = false;
    rs.filter = PixelFilter{PixelFilterType::Box, 1.0f};

    Renderer renderer;
    RenderPassFilms passes;
    AovFilms aovs;
    Image beauty = renderer.render(scene, cam, rs, nullptr, &passes, &aovs);

    const float hashA = cryptoNameToFloat("Hero_A");
    const float hashB = cryptoNameToFloat("Hero_B");

    // Sol iç piksel: %100 Hero_A
    const auto leftRanks = passes.cryptoObject.pixelRanks(10, 16);
    ASSERT_FALSE(leftRanks.empty());
    EXPECT_FLOAT_EQ(leftRanks[0].first, hashA);
    EXPECT_NEAR(leftRanks[0].second, 1.0f, 1e-4f);

    // Sağ iç piksel: %100 Hero_B
    const auto rightRanks = passes.cryptoObject.pixelRanks(22, 16);
    ASSERT_FALSE(rightRanks.empty());
    EXPECT_FLOAT_EQ(rightRanks[0].first, hashB);
    EXPECT_NEAR(rightRanks[0].second, 1.0f, 1e-4f);

    // Orta sınır pikseli (x=16, y=16): Hero_A + Hero_B karışık kapsama, toplam = 1.0
    const auto seamRanks = passes.cryptoObject.pixelRanks(16, 16);
    ASSERT_GE(seamRanks.size(), 2u);
    EXPECT_GE(seamRanks[0].second, seamRanks[1].second);
    EXPECT_GT(seamRanks[1].second, 0.1f);
    EXPECT_NEAR(seamRanks[0].second + seamRanks[1].second, 1.0f, 1e-4f);

    // Köşe arka plan pikseli: boş
    const auto bgRanks = passes.cryptoObject.pixelRanks(0, 0);
    EXPECT_TRUE(bgRanks.empty());

    // Çok katmanlı EXR gidiş-dönüş: kanallar + Psyop Cryptomatte manifest üstverisi
    const std::string exrPath = (std::filesystem::temp_directory_path() / "photon_test_cryptomatte.exr").string();
    std::vector<Image> storage;
    std::vector<CryptoRankImage> cryptoStorage;
    const auto layers = makeExrLayers(&passes, &aovs, storage, &cryptoStorage);
    const auto meta = makeExrCryptoMetadata(&passes);
    const Image alpha = aovs.alpha.resolve();
    ASSERT_TRUE(saveMultiLayerEXR(exrPath, beauty, &alpha, layers, meta));

    const auto channels = readExrChannelNames(exrPath);
    EXPECT_TRUE(hasChannel(channels, "CryptoObject00.R"));
    EXPECT_TRUE(hasChannel(channels, "CryptoObject00.G"));
    EXPECT_TRUE(hasChannel(channels, "CryptoObject00.B"));
    EXPECT_TRUE(hasChannel(channels, "CryptoObject00.A"));
    EXPECT_TRUE(hasChannel(channels, "CryptoObject02.A"));
    EXPECT_TRUE(hasChannel(channels, "CryptoMaterial00.R"));
    EXPECT_TRUE(hasChannel(channels, "CryptoMaterial00.A"));

    const auto attrs = readExrStringAttributes(exrPath);
    const std::string objManifestKey = cryptoMetadataPrefix("CryptoObject") + "manifest";
    const std::string matManifestKey = cryptoMetadataPrefix("CryptoMaterial") + "manifest";
    std::string objManifest, matManifest;
    for (const auto& [k, v] : attrs) {
        if (k == objManifestKey) objManifest = v;
        if (k == matManifestKey) matManifest = v;
    }
    EXPECT_NE(objManifest.find("\"Hero_A\""), std::string::npos);
    EXPECT_NE(objManifest.find("\"Hero_B\""), std::string::npos);
    EXPECT_NE(matManifest.find("\"Mat_Red\""), std::string::npos);
    EXPECT_NE(matManifest.find("\"Mat_Blue\""), std::string::npos);
    std::filesystem::remove(exrPath);
}

TEST(PathGuiding, DirectionalTreeSamplePdfConsistency) {
    DirectionalTree build;
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    // İki baskın yöne yoğun radyans splat et + düşük arka plan tabanı
    for (int i = 0; i < 4000; ++i) {
        const Vec3f d1 = Vec3f(0.8f + 0.1f * (dist(rng) - 0.5f), 0.5f + 0.1f * (dist(rng) - 0.5f), 0.1f).normalized();
        build.addSplat(guidingDirToCanonical(d1), 10.0f);
        const Vec3f dBg = guidingCanonicalToDir(Vec2f(dist(rng), dist(rng)));
        build.addSplat(guidingDirToCanonical(dBg), 0.1f);
    }

    DirectionalTree tree;
    tree.buildFrom(build, 0.01f, 8);
    ASSERT_TRUE(tree.valid());

    // 1. sample() ve pdf() aynı dağılımı vermeli ve ∫ (1/p(ω)) p(ω) dω = 4π olmalı
    double invPdfSum = 0.0;
    constexpr int N = 20000;
    for (int i = 0; i < N; ++i) {
        float pdfSample = 0.0f;
        const Vec3f dir = tree.sample(Vec2f(dist(rng), dist(rng)), pdfSample);
        const float pdfEval = tree.pdf(dir);
        ASSERT_GT(pdfSample, 0.0f);
        EXPECT_NEAR(pdfSample, pdfEval, pdfSample * 1e-4f + 1e-6f);
        invPdfSum += 1.0 / static_cast<double>(pdfSample);
    }
    const double areaEstimate = invPdfSum / static_cast<double>(N);
    const double fourPi = 4.0 * static_cast<double>(PI);
    EXPECT_NEAR(areaEstimate, fourPi, fourPi * 0.03);
}

TEST(PathGuiding, SdTreeReducesIndirectVarianceAndIsUnbiased) {
    // Saf dolaylı aydınlatma odası:
    // Arka duvar (z = -2) yalnız sağ duvardaki küçük parlak beyaz yansıtıcı levhadan
    // (x = 1.98, y ∈ [-0.45, 0.45], z ∈ [-0.45, 0.45]) seken dolaylı ışığı görür;
    // ara perde (z = -0.7, x = 0.4) gizli alan ışığının arka duvara doğrudan görüş
    // hattını %100 kapatır → arka duvarda NEE = 0, tüm aydınlanma dar konili saf GI!
    Scene scene;
    auto whiteWall = std::make_shared<Lambertian>(Color3f(0.8f, 0.8f, 0.8f));
    auto darkWall = std::make_shared<Lambertian>(Color3f(0.02f, 0.02f, 0.02f));
    scene.retainMaterial(whiteWall);
    scene.retainMaterial(darkWall);

    // Arka duvar (hedef): z = -2, normal +z
    scene.addShape(makeQuadMesh(Vec3f(-2, -2, -2), Vec3f(2, -2, -2), Vec3f(2, 2, -2), Vec3f(-2, 2, -2), whiteWall.get()));
    // Zemin (y = -2), sol duvar (x = -2), sağ duvar (x = +2), tavan (y = +2)
    scene.addShape(makeQuadMesh(Vec3f(-2, -2, -2), Vec3f(-2, -2, 2), Vec3f(2, -2, 2), Vec3f(2, -2, -2), darkWall.get()));
    scene.addShape(makeQuadMesh(Vec3f(-2, -2, 2), Vec3f(-2, -2, -2), Vec3f(-2, 2, -2), Vec3f(-2, 2, 2), darkWall.get()));
    scene.addShape(makeQuadMesh(Vec3f(2, -2, -2), Vec3f(2, -2, 2), Vec3f(2, 2, 2), Vec3f(2, 2, -2), darkWall.get()));
    scene.addShape(makeQuadMesh(Vec3f(-2, 2, -2), Vec3f(2, 2, -2), Vec3f(2, 2, 2), Vec3f(-2, 2, 2), darkWall.get()));

    // Sağ duvarda küçük parlak beyaz yansıtıcı levha (gizli ışıkla aydınlanır)
    scene.addShape(makeQuadMesh(Vec3f(1.98f, -0.45f, -0.45f), Vec3f(1.98f, -0.45f, 0.45f),
                                Vec3f(1.98f, 0.45f, 0.45f), Vec3f(1.98f, 0.45f, -0.45f), whiteWall.get()));
    // Perde (z = -0.7 ve x = 0.4): gizli ışığı arka duvardan tamamen gizler
    scene.addShape(makeQuadMesh(Vec3f(0.4f, 0.5f, -0.7f), Vec3f(2.0f, 0.5f, -0.7f),
                                Vec3f(2.0f, 2.0f, -0.7f), Vec3f(0.4f, 2.0f, -0.7f), darkWall.get()));
    scene.addShape(makeQuadMesh(Vec3f(0.4f, 0.5f, -0.7f), Vec3f(0.4f, 0.5f, 0.7f),
                                Vec3f(0.4f, 2.0f, 0.7f), Vec3f(0.4f, 2.0f, -0.7f), darkWall.get()));
    auto hiddenLight = std::make_shared<AreaLight>(
        Vec3f(0.9f, 1.4f, -0.35f), Vec3f(0.6f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 0.7f),
        Color3f(80.0f, 80.0f, 80.0f));
    hiddenLight->setVisibleToCamera(false);
    scene.addLight(hiddenLight);
    scene.buildAccelerator();

    PerspectiveCamera cam(Vec3f(-0.4f, -0.2f, 1.4f), Vec3f(-0.4f, -0.2f, -2.0f), Vec3f(0.0f, 1.0f, 0.0f), 36.0f, 1.0f);
    RenderSettings base;
    base.width = 32;
    base.height = 32;
    base.samplesPerPixel = 96;
    base.maxBounces = 3;
    base.denoiseEnabled = false;
    base.filter = PixelFilter{PixelFilterType::Box, 1.0f};

    Renderer refRenderer;
    RenderSettings refRs = base;
    refRs.samplesPerPixel = 512;
    refRs.pathGuiding = false;
    Image imgRef = refRenderer.render(scene, cam, refRs);

    Renderer unguidedRenderer;
    base.pathGuiding = false;
    Image imgUnguided = unguidedRenderer.render(scene, cam, base);

    Renderer guidedRenderer;
    RenderSettings guidedRs = base;
    guidedRs.pathGuiding = true;
    Image imgGuided = guidedRenderer.render(scene, cam, guidedRs);

    ASSERT_NE(guidedRenderer.guidingField(), nullptr);
    EXPECT_TRUE(guidedRenderer.guidingField()->ready());

    auto statsVsRef = [&](const Image& img) -> std::pair<double, double> {
        double sum = 0.0, mse = 0.0;
        int n = 0;
        for (int y = 6; y < 26; ++y) {
            for (int x = 6; x < 26; ++x) {
                const double v = img.getPixel(x, y).luminance();
                const double r = imgRef.getPixel(x, y).luminance();
                sum += v;
                mse += (v - r) * (v - r);
                ++n;
            }
        }
        return {sum / static_cast<double>(n), mse / static_cast<double>(n)};
    };

    const auto [meanRef, zeroMse] = statsVsRef(imgRef);
    (void)zeroMse;
    const auto [meanU, mseU] = statsVsRef(imgUnguided);
    const auto [meanG, mseG] = statsVsRef(imgGuided);

    EXPECT_GT(meanRef, 0.01);
    // Yansızlık: yönlendirmeli ortalama, 512-spp referansla %6 içinde tutarlı olmalı!
    EXPECT_NEAR(meanG, meanRef, meanRef * 0.06);
    // Varyans azaltımı: yönlendirmeli MSE, yönlendirmesiz MSE'den düşük olmalı!
    EXPECT_LT(mseG, mseU);
}
