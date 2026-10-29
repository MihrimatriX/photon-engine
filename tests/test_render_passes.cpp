// Render geçişleri testleri: ışık geçişlerinin toplamı her pikselde beauty'ye eşit (compositing'de
// geçişlerden beauty yeniden kurulabilmeli), her lob grubu doğru yere gidiyor (metal → parlak,
// zemin → difüz, cam → geçirgen, görünen ışık → emisyon, gök → ortam) ve çok katmanlı EXR
// Nuke/Blender kanal adlarıyla yazılıyor.

#include <gtest/gtest.h>
#include "camera/perspective_camera.h"
#include "core/image/image_io.h"
#include "engine/renderer.h"
#include "engine/scene.h"
#include "geometry/mesh.h"
#include "geometry/sphere.h"
#include "lights/area_light.h"
#include "lights/environment_light.h"
#include "materials/dielectric.h"
#include "materials/disney.h"
#include "materials/lambertian.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <vector>

using namespace photon;

namespace {

struct PassScene {
    Scene scene;
    std::unique_ptr<PerspectiveCamera> camera;
};

void build(PassScene& s) {
    static const Lambertian floorMat(Color3f(0.6f));
    static const DisneyMaterial metal(Color3f(0.95f, 0.8f, 0.5f), 1.0f, 0.25f, 0.5f);
    static const Dielectric glass(1.5f, Color3f(1.0f), 0.0f);
    static const Lambertian emitter(Color3f(0.0f), Color3f(6.0f));
    const Vec3f up(0, 1, 0);
    std::vector<Vec3f> fp = {{-4, 0, -4}, {4, 0, -4}, {4, 0, 4}, {-4, 0, 4}};
    s.scene.addShape(std::make_shared<TriangleMesh>(fp, std::vector<Vec3f>{up, up, up, up}, std::vector<Vec2f>{},
                                                    std::vector<uint32_t>{0, 2, 1, 0, 3, 2}, &floorMat));
    s.scene.addShape(std::make_shared<Sphere>(Vec3f(-0.8f, 0.6f, 0.0f), 0.6f, &metal));
    s.scene.addShape(std::make_shared<Sphere>(Vec3f(0.8f, 0.6f, 0.0f), 0.6f, &glass));
    std::vector<Vec3f> ep = {{-0.4f, 1.8f, -1.5f}, {0.4f, 1.8f, -1.5f}, {0.4f, 2.4f, -1.5f}, {-0.4f, 2.4f, -1.5f}};
    s.scene.addShape(std::make_shared<TriangleMesh>(ep, std::vector<Vec3f>{}, std::vector<Vec2f>{},
                                                    std::vector<uint32_t>{0, 1, 2, 0, 2, 3}, &emitter));
    s.scene.addLight(std::make_shared<AreaLight>(Vec3f(-2.0f, 3.0f, 1.0f), Vec3f(1, 0, 0), Vec3f(0, 0, 1), Color3f(8.0f)));
    s.scene.setEnvironment(std::make_shared<EnvironmentLight>(nullptr, 0.0f, 0.4f));
    s.scene.buildAccelerator();
    s.camera = std::make_unique<PerspectiveCamera>(Vec3f(0, 1.4f, 4.5f), Vec3f(0, 0.8f, 0), up, 45.0f, 4.0f / 3.0f);
}

float total(const Image& img) {
    double s = 0.0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) s += img.getPixel(x, y).luminance();
    return static_cast<float>(s);
}

} // namespace

TEST(RenderPasses, LightPassesSumToBeautyAndLandInTheRightPlace) {
    PassScene s;
    build(s);
    RenderSettings rs;
    rs.width = 64;
    rs.height = 48;
    rs.samplesPerPixel = 16;
    rs.maxBounces = 6;
    rs.filter = PixelFilter{PixelFilterType::Box, 1.0f};
    RenderPassFilms passes;
    AovFilms aovs;
    Renderer r;
    const Image beauty = r.render(s.scene, *s.camera, rs, nullptr, &passes, &aovs);

    std::vector<Image> light;
    for (const Film& f : passes.light) light.push_back(f.resolve());
    float worst = 0.0f;
    for (int y = 0; y < rs.height; ++y)
        for (int x = 0; x < rs.width; ++x) {
            Color3f sum(0.0f);
            for (const Image& im : light) sum += im.getPixel(x, y);
            const Color3f b = beauty.getPixel(x, y);
            for (int c = 0; c < 3; ++c)
                worst = std::max(worst, std::abs(sum[c] - b[c]) / std::max(1.0f, b[c]));
        }
    EXPECT_LT(worst, 1e-4f) << "geçişlerin toplamı beauty'den sapıyor";

    auto passTotal = [&](LightPass p) { return total(light[static_cast<int>(p)]); };
    EXPECT_GT(passTotal(LightPass::DiffuseDirect), 0.0f);
    EXPECT_GT(passTotal(LightPass::DiffuseIndirect), 0.0f);
    EXPECT_GT(passTotal(LightPass::GlossyDirect), 0.0f);
    EXPECT_GT(passTotal(LightPass::TransmissionIndirect), 0.0f); // cam küre, arkasındaki sahne
    EXPECT_GT(passTotal(LightPass::Emission), 0.0f);              // kameradan görülen yayıcı
    EXPECT_GT(passTotal(LightPass::Background), 0.0f);            // gök

    // Metal kürenin ortası: parlak geçişler difüzden baskın (metal difüz yansıtmaz).
    const Vec2f metalPx(0.37f * rs.width, 0.52f * rs.height);
    const int mx = static_cast<int>(metalPx.x), my = static_cast<int>(metalPx.y);
    const float gloss = light[static_cast<int>(LightPass::GlossyDirect)].getPixel(mx, my).luminance() +
                        light[static_cast<int>(LightPass::GlossyIndirect)].getPixel(mx, my).luminance();
    const float diff = light[static_cast<int>(LightPass::DiffuseDirect)].getPixel(mx, my).luminance() +
                       light[static_cast<int>(LightPass::DiffuseIndirect)].getPixel(mx, my).luminance();
    EXPECT_GT(gloss, 10.0f * diff + 1e-4f);

    // Veri geçişleri: kürelerin derinliği kamera uzaklığıyla tutarlı, nesne kimlikleri dolu.
    const Image depth = passes.depth.resolve();
    EXPECT_NEAR(depth.getPixel(mx, my).r, 4.0f, 0.6f);

    // Çok katmanlı EXR: Nuke/Blender kanal adları, sıralı başlık.
    std::vector<Image> storage;
    const auto layers = makeExrLayers(&passes, &aovs, storage);
    const std::string path = (std::filesystem::temp_directory_path() / "photon_passes.exr").string();
    const Image alpha = aovs.alpha.resolve();
    ASSERT_TRUE(saveMultiLayerEXR(path, beauty, &alpha, layers));
    const std::vector<std::string> names = readExrChannelNames(path);
    for (const char* want : {"A", "B", "G", "R", "DiffDir.R", "GlossInd.G", "TransInd.B", "Emit.R", "Env.R",
                             "Depth.Z", "Position.X", "Normal.Z", "DiffCol.G", "ObjectID.R", "MaterialID.B"})
        EXPECT_NE(std::find(names.begin(), names.end(), want), names.end()) << want;
    EXPECT_TRUE(std::is_sorted(names.begin(), names.end()));
    // Beauty katmanı normal EXR okuyucusuyla da açılır.
    auto back = loadImageEXR(path);
    ASSERT_TRUE(back.has_value());
    EXPECT_NEAR(total(*back) / total(beauty), 1.0f, 0.01f);
}
