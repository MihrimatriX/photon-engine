// test_texture_lod.cpp — Kompakt 8-bit doku belleği, doğrusal mipmap piramidi, ışın konisi
// (Ray Cone) LOD anti-aliasing ve Birleşik OpenPBR / glTF 2.0 parametre testleri.

#include <gtest/gtest.h>
#include "camera/perspective_camera.h"
#include "core/image/image.h"
#include "core/image/image_io.h"
#include "core/math/constants.h"
#include "core/random/rng.h"
#include "engine/renderer.h"
#include "engine/scene.h"
#include "geometry/mesh.h"
#include "integrators/path_tracer.h"
#include "io/gltf_loader.h"
#include "lights/directional_light.h"
#include "materials/disney.h"
#include "scene/material_library.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

using namespace photon;

namespace {

std::vector<uint8_t> makeCheckerRgb8(int w, int h, int cell) {
    std::vector<uint8_t> buf(static_cast<size_t>(w) * static_cast<size_t>(h) * 3u);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool white = (((x / cell) + (y / cell)) & 1) == 0;
            const uint8_t v = white ? 255u : 0u;
            const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 3u;
            buf[i + 0] = v;
            buf[i + 1] = v;
            buf[i + 2] = v;
        }
    }
    return buf;
}

std::shared_ptr<TriangleMesh> makeFloorQuad(float halfW, float zNear, float zFar, float y, const Material* mat) {
    std::vector<Vec3f> positions = {
        Vec3f(-halfW, y, zNear),
        Vec3f( halfW, y, zNear),
        Vec3f( halfW, y, zFar),
        Vec3f(-halfW, y, zFar),
    };
    std::vector<Vec3f> normals = {
        Vec3f(0, 1, 0),
        Vec3f(0, 1, 0),
        Vec3f(0, 1, 0),
        Vec3f(0, 1, 0),
    };
    std::vector<Vec2f> uvs = {
        Vec2f(0.0f, 0.0f),
        Vec2f(1.0f, 0.0f),
        Vec2f(1.0f, 1.0f),
        Vec2f(0.0f, 1.0f),
    };
    std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
    return std::make_shared<TriangleMesh>(positions, normals, uvs, indices, mat);
}

float imageSpatialVariance(const Image& img, int yMin, int yMax) {
    double sumDiffSq = 0.0;
    int count = 0;
    for (int y = yMin; y < yMax; ++y) {
        for (int x = 1; x < img.width(); ++x) {
            const float a = img.getPixel(x - 1, y).luminance();
            const float b = img.getPixel(x, y).luminance();
            const double d = static_cast<double>(a - b);
            sumDiffSq += d * d;
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(sumDiffSq / count) : 0.0f;
}

} // namespace

TEST(TextureCompact8, MemorySavingsAndSrgbAccuracy) {
    constexpr int W = 64, H = 64;
    auto checker = makeCheckerRgb8(W, H, 1);

    Image f32(W, H);
    Image u8srgb = Image::createCompact8(W, H, checker.data(), Image::StorageFormat::U8Srgb);
    Image u8lin = Image::createCompact8(W, H, checker.data(), Image::StorageFormat::U8Linear);

    EXPECT_FALSE(f32.isCompact8());
    EXPECT_TRUE(u8srgb.isCompact8());
    EXPECT_TRUE(u8lin.isCompact8());

    // Float32 = 12 bayt/piksel, U8 = 3 bayt/piksel → tam 4× bellek tasarrufu.
    EXPECT_EQ(f32.memoryBytes(), static_cast<size_t>(W * H * 12));
    EXPECT_EQ(u8srgb.memoryBytes(), static_cast<size_t>(W * H * 3));
    EXPECT_LT(u8srgb.memoryBytes() * 3, f32.memoryBytes());

    // sRGB → doğrusal LUT doğruluğu (orta gri 186/255 ≈ 0.491 doğrusal).
    std::vector<uint8_t> midRgb = {186, 128, 64};
    Image single = Image::createCompact8(1, 1, midRgb.data(), Image::StorageFormat::U8Srgb);
    Color3f c = single.getPixel(0, 0);
    auto exactSrgb = [](uint8_t u) {
        float s = u / 255.0f;
        return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
    };
    EXPECT_NEAR(c.r, exactSrgb(186), 1e-4f);
    EXPECT_NEAR(c.g, exactSrgb(128), 1e-4f);
    EXPECT_NEAR(c.b, exactSrgb(64), 1e-4f);

    // Mipmap piramidi: 1x1 dama tahtası yüksek LOD'da doğrusal ortalamaya (0.5) yakınsar.
    u8srgb.buildMipmaps();
    EXPECT_TRUE(u8srgb.hasMipmaps());
    EXPECT_GE(u8srgb.mipLevels(), 6);
    const Color3f avg = u8srgb.sampleMip(0.37f, 0.61f, 6.0f);
    EXPECT_NEAR(avg.r, 0.5f, 0.02f);
    EXPECT_NEAR(avg.g, 0.5f, 0.02f);
    EXPECT_NEAR(avg.b, 0.5f, 0.02f);
}

TEST(RayConeLod, ReducesHighFrequencyCheckerAliasing) {
    constexpr int T = 64;
    auto checker = makeCheckerRgb8(T, T, 1);

    auto texNoMip = std::make_shared<Image>(
        Image::createCompact8(T, T, checker.data(), Image::StorageFormat::U8Linear));
    auto texWithMip = std::make_shared<Image>(
        Image::createCompact8(T, T, checker.data(), Image::StorageFormat::U8Linear));
    texWithMip->buildMipmaps();

    auto renderWithTex = [](std::shared_ptr<Image> tex) {
        Scene scene;
        auto mat = std::make_shared<DisneyMaterial>(Color3f(1.0f), 0.0f, 0.9f, 0.0f);
        mat->setAlbedoImage(std::move(tex));
        mat->setTextureScale(24.0f); // yüksek uzamsal frekans → uzak bölgede güçlü aliasing
        auto floor = makeFloorQuad(20.0f, -1.0f, -80.0f, 0.0f, mat.get());
        scene.retainMaterial(mat);
        scene.addShape(floor);
        // Tek yönlü ışık + maxBounces = 1: sıfır aydınlatma gürültüsü → yalnız doku aliasing'i ölçülür.
        scene.addLight(std::make_shared<DirectionalLight>(Vec3f(0.0f, -1.0f, 0.0f), Color3f(PI)));
        scene.buildAccelerator();

        PerspectiveCamera cam(Vec3f(0.0f, 1.2f, 0.5f), Vec3f(0.0f, 0.0f, -12.0f),
                              Vec3f(0.0f, 1.0f, 0.0f), 45.0f, 1.0f);
        RenderSettings rs;
        rs.width = 64;
        rs.height = 64;
        rs.samplesPerPixel = 1;
        rs.maxBounces = 1;
        rs.numThreads = 1;
        rs.filter.type = PixelFilterType::Box;
        rs.filter.width = 1.0f;

        Renderer renderer;
        return renderer.render(scene, cam, rs);
    };

    Image imgNoMip = renderWithTex(texNoMip);
    Image imgWithMip = renderWithTex(texWithMip);

    // Görüntünün ufka yakın zemin satırları (y = 32..46)
    const float varNoMip = imageSpatialVariance(imgNoMip, 32, 46);
    const float varWithMip = imageSpatialVariance(imgWithMip, 32, 46);
    EXPECT_GT(varNoMip, 1e-2f);
    EXPECT_LT(varWithMip, varNoMip * 0.25f)
        << "Ray Cone LOD mipmap süzgeci uzak dama tahtası aliasing varyansını düşürmeli";
}

TEST(OpenPBR, UnifiedTransmissionIorCoatAndGltfRoundTrip) {
    // 1. Birleşik OpenPBR kırılma + vernik tonu + özel sheen testi
    constexpr float kIor = 1.52f;
    DisneyMaterial glassLike(Color3f(0.95f, 0.98f, 1.0f), 0.0f, 0.08f, 0.5f);
    glassLike.setIor(kIor);
    glassLike.setSpecularTransmission(0.9f);
    glassLike.setClearCoat(0.5f);
    glassLike.setClearCoatRoughness(0.05f);
    glassLike.setCoatIor(1.65f);
    glassLike.setCoatTint(Color3f(0.92f, 0.96f, 1.0f));
    glassLike.setSheen(0.3f);
    glassLike.setSheenColor(Color3f(0.8f, 0.4f, 0.9f));
    EXPECT_TRUE(glassLike.isVolumeBoundary());

    SurfaceInteraction si;
    si.normal = Vec3f(0, 0, 1);
    si.ng = si.normal;
    si.tangent = Vec3f(1, 0, 0);
    si.frontFace = true;
    const Vec3f wo = Vec3f(0.3f, 0.0f, 0.953939f).normalized();

    RNG rng(1337);
    double reflEnergy = 0.0, transEnergy = 0.0;
    constexpr int N = 4000;
    for (int i = 0; i < N; ++i) {
        BSDFSample bs;
        if (glassLike.sampleFull(wo, si, rng.uniformFloat(), rng.uniformFloat2D(), bs) && bs.pdf > 0.0f) {
            const float contrib = bs.f.luminance() * std::abs(bs.wi.z) / bs.pdf;
            if (bs.wi.z >= 0.0f) reflEnergy += contrib;
            else transEnergy += contrib;
        }
    }
    // Radyans ölçeklemesi (Veach §5.2): havadan cama girişte L_t / L_i = 1 / n².
    // Akı korunumu: R + T * n² ≤ 1.
    const double transAvg = transEnergy / N;
    const double fluxConserved = (reflEnergy + transEnergy * kIor * kIor) / N;
    EXPECT_GT(transAvg, 0.28);
    EXPECT_GT(fluxConserved, 0.75);
    EXPECT_LE(fluxConserved, 1.05);

    // 2. Preset & JSON gidiş-dönüş (backward-compatible)
    MaterialPreset p = MaterialLibrary::presetFromMaterial(glassLike, "OpenPBR Cam");
    EXPECT_NEAR(p.ior, 1.52f, 1e-4f);
    EXPECT_NEAR(p.specularTransmission, 0.9f, 1e-4f);
    EXPECT_NEAR(p.coatIor, 1.65f, 1e-4f);
    EXPECT_NEAR(p.coatTint.r, 0.92f, 1e-4f);
    EXPECT_TRUE(p.hasCustomSheenColor);
    EXPECT_NEAR(p.sheenColor.g, 0.4f, 1e-4f);

    nlohmann::json j = nlohmann::json::object();
    presetAdvancedToJson(p, j);
    MaterialPreset p2;
    presetAdvancedFromJson(j, p2);
    EXPECT_NEAR(p2.ior, 1.52f, 1e-4f);
    EXPECT_NEAR(p2.specularTransmission, 0.9f, 1e-4f);
    EXPECT_NEAR(p2.coatIor, 1.65f, 1e-4f);
    EXPECT_NEAR(p2.coatTint.r, 0.92f, 1e-4f);
    EXPECT_TRUE(p2.hasCustomSheenColor);
    EXPECT_NEAR(p2.sheenColor.b, 0.9f, 1e-4f);

    auto recreated = MaterialLibrary::createMaterial(p2);
    const auto* d2 = dynamic_cast<const DisneyMaterial*>(recreated.get());
    ASSERT_NE(d2, nullptr);
    EXPECT_NEAR(d2->ior(), 1.52f, 1e-4f);
    EXPECT_NEAR(d2->specularTransmission(), 0.9f, 1e-4f);
    EXPECT_NEAR(d2->coatIor(), 1.65f, 1e-4f);

    // 3. glTF 2.0 KHR uzantıları yükleme gidiş-dönüşü
    namespace fs = std::filesystem;
    const fs::path tmpGltf = fs::temp_directory_path() / "photon_test_openpbr_khr.gltf";
    // Minimal 1-üçgen gömülü data URI glTF 2.0
    // 3 * Vec3f positions (36 bytes): (0,0,0), (1,0,0), (0,1,0)
    // Base64 of 36 bytes: AAAAAAAAAAAAAAAA//8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAA== (exact computed below)
    const char* gltfSrc = R"({
      "asset": { "version": "2.0" },
      "extensionsUsed": [
        "KHR_materials_ior",
        "KHR_materials_transmission",
        "KHR_materials_clearcoat",
        "KHR_materials_sheen"
      ],
      "buffers": [{
        "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA",
        "byteLength": 36
      }],
      "bufferViews": [{ "buffer": 0, "byteOffset": 0, "byteLength": 36 }],
      "accessors": [{
        "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
        "min": [0, 0, 0], "max": [1, 1, 0]
      }],
      "materials": [{
        "name": "OpenPbrGltfMat",
        "pbrMetallicRoughness": {
          "baseColorFactor": [0.8, 0.9, 1.0, 1.0],
          "metallicFactor": 0.0,
          "roughnessFactor": 0.15
        },
        "extensions": {
          "KHR_materials_ior": { "ior": 1.6 },
          "KHR_materials_transmission": { "transmissionFactor": 0.75 },
          "KHR_materials_clearcoat": { "clearcoatFactor": 0.6, "clearcoatRoughnessFactor": 0.08 },
          "KHR_materials_sheen": { "sheenColorFactor": [0.9, 0.7, 0.5], "sheenRoughnessFactor": 0.4 }
        }
      }],
      "meshes": [{
        "primitives": [{ "attributes": { "POSITION": 0 }, "material": 0 }]
      }],
      "nodes": [{ "mesh": 0 }],
      "scenes": [{ "nodes": [0] }],
      "scene": 0
    })";
    {
        std::ofstream out(tmpGltf);
        out << gltfSrc;
    }
    GltfLoadResult loaded = GltfLoader::load(tmpGltf.string(), nullptr);
    std::error_code ec;
    fs::remove(tmpGltf, ec);
    ASSERT_EQ(loaded.meshes.size(), 1u);
    ASSERT_FALSE(loaded.materials.empty());
    const auto* gm = dynamic_cast<const DisneyMaterial*>(loaded.meshes[0]->material());
    ASSERT_NE(gm, nullptr);
    EXPECT_NEAR(gm->ior(), 1.6f, 1e-4f);
    EXPECT_NEAR(gm->specularTransmission(), 0.75f, 1e-4f);
    EXPECT_NEAR(gm->clearCoat(), 0.6f, 1e-4f);
    EXPECT_NEAR(gm->clearCoatRoughness(), 0.08f, 1e-4f);
    EXPECT_GT(gm->sheen(), 0.5f);
    EXPECT_TRUE(gm->hasCustomSheenColor());
}
