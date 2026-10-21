// test_loader_limits.cpp — Güvenilmez girdi testleri: boş, kesik, sınır dışı indeksli, NaN/Inf
// içeren ya da devasa sayım bildiren OBJ/glTF/HDR/PNG/EXR/proje dosyaları çökmeden
// reddedilmeli; geçerli küçük dosyalar ise yüklenmeli. Ayrıca proje kaydet/aç gidiş-dönüşü
// (Türkçe karakterli UTF-8 yol dahil).
#include <gtest/gtest.h>
#include "core/image/image.h"
#include "core/image/image_io.h"
#include "io/gltf_loader.h"
#include "io/obj_loader.h"
#include "scene/project_io.h"
#include "scene/scene_graph.h"
#include "materials/disney.h"
#include "core/platform/path.h"
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace photon;

namespace {

std::filesystem::path scratchDir(const char* name) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void writeBytes(const std::filesystem::path& path, const void* data, size_t n) {
    std::ofstream out(path, std::ios::binary);
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(n));
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    writeBytes(path, text.data(), text.size());
}

// Radiance .hdr (RGBE) üretir. Genişlik 8..32767 ise yeni tip satır RLE'si kullanılır:
// satır başına (2, 2, genişlik_hi, genişlik_lo), sonra her kanal için (128+koşu, değer) çiftleri.
std::string rleHdr(int width, int height) {
    std::string s = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y " + std::to_string(height)
        + " +X " + std::to_string(width) + "\n";
    for (int y = 0; y < height; ++y) {
        if (width < 8 || width >= 32768) {
            for (int x = 0; x < width; ++x) {
                s.push_back(0);
                s.push_back(0);
                s.push_back(0);
                s.push_back(static_cast<char>(128));
            }
            continue;
        }
        s.push_back(2);
        s.push_back(2);
        s.push_back(static_cast<char>((width >> 8) & 0xff));
        s.push_back(static_cast<char>(width & 0xff));
        for (int c = 0; c < 4; ++c) {
            int left = width;
            while (left > 0) {
                int run = left > 127 ? 127 : left;
                s.push_back(static_cast<char>(128 + run));
                s.push_back(c == 3 ? static_cast<char>(128) : 0);
                left -= run;
            }
        }
    }
    return s;
}

void writeTriangleGltf(const std::filesystem::path& dir, const float verts[9], const uint32_t idx[3]) {
    std::vector<unsigned char> bytes(48, 0);
    std::memcpy(bytes.data(), verts, 9 * sizeof(float));
    std::memcpy(bytes.data() + 36, idx, 3 * sizeof(uint32_t));
    writeBytes(dir / "buf.bin", bytes.data(), bytes.size());
    writeText(dir / "tri.gltf", R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 1, "componentType": 5125, "count": 3, "type": "SCALAR"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 12}
  ],
  "buffers": [{"uri": "buf.bin", "byteLength": 48}]
})");
}

// Belleğe gömülü en küçük geçerli 1×1 RGB PNG (dosya ve bellekten yükleme testi için).
const unsigned char kPng1x1[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
    0xDE, 0x00, 0x00, 0x00, 0x0F, 0x49, 0x44, 0x41, 0x54, 0x78, 0x01, 0x01, 0x04, 0x00, 0xFB, 0xFF,
    0x00, 0xFF, 0x00, 0x00, 0x03, 0x01, 0x01, 0x00, 0x8D, 0x1D, 0xE5, 0x82, 0x00, 0x00, 0x00, 0x00,
    0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

} // namespace

TEST(LoaderLimits, ObjCorruptRejected) {
    namespace fs = std::filesystem;
    fs::path dir = scratchDir("photon_limits_obj_bad");
    writeText(dir / "empty.obj", "");
    writeText(dir / "trunc.obj", "v 0 0\n");
    writeText(dir / "huge.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 999999999\n");
    writeText(dir / "inf.obj", "v 1e999 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

    EXPECT_TRUE(ObjLoader::load((dir / "empty.obj").string(), nullptr).meshes.empty());
    EXPECT_TRUE(ObjLoader::load((dir / "trunc.obj").string(), nullptr).meshes.empty());
    EXPECT_TRUE(ObjLoader::load((dir / "huge.obj").string(), nullptr).meshes.empty());
    EXPECT_TRUE(ObjLoader::load((dir / "inf.obj").string(), nullptr).meshes.empty());
    fs::remove_all(dir);
}

TEST(LoaderLimits, ObjTriangleLoads) {
    namespace fs = std::filesystem;
    fs::path dir = scratchDir("photon_limits_obj_ok");
    writeText(dir / "tri.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    auto loaded = ObjLoader::load((dir / "tri.obj").string(), nullptr);
    fs::remove_all(dir);
    ASSERT_EQ(loaded.meshes.size(), 1u);
    EXPECT_EQ(loaded.meshes[0]->numTriangles(), 1u);
}

TEST(LoaderLimits, GltfCorruptRejected) {
    namespace fs = std::filesystem;
    fs::path dir = scratchDir("photon_limits_gltf_bad");
    writeText(dir / "empty.gltf", "");
    writeText(dir / "trunc.gltf", "{");

    float verts[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    uint32_t badIdx[3] = {0, 1, 1000000};
    fs::path idxDir = dir / "idx";
    fs::create_directories(idxDir);
    writeTriangleGltf(idxDir, verts, badIdx);

    float nanVerts[9] = {std::numeric_limits<float>::quiet_NaN(), 0, 0, 1, 0, 0, 0, 1, 0};
    uint32_t okIdx[3] = {0, 1, 2};
    fs::path nanDir = dir / "nan";
    fs::create_directories(nanDir);
    writeTriangleGltf(nanDir, nanVerts, okIdx);

    fs::path countDir = dir / "count";
    fs::create_directories(countDir);
    unsigned char tiny[12] = {};
    writeBytes(countDir / "buf.bin", tiny, sizeof(tiny));
    writeText(countDir / "count.gltf", R"({
  "asset": {"version": "2.0"},
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
  "accessors": [{"bufferView": 0, "componentType": 5126, "count": 999999999, "type": "VEC3"}],
  "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 12}],
  "buffers": [{"uri": "buf.bin", "byteLength": 12}]
})");

    EXPECT_TRUE(GltfLoader::load((dir / "empty.gltf").string(), nullptr).meshes.empty());
    EXPECT_TRUE(GltfLoader::load((dir / "trunc.gltf").string(), nullptr).meshes.empty());
    EXPECT_TRUE(GltfLoader::load((idxDir / "tri.gltf").string(), nullptr).meshes.empty());
    EXPECT_TRUE(GltfLoader::load((nanDir / "tri.gltf").string(), nullptr).meshes.empty());
    EXPECT_TRUE(GltfLoader::load((countDir / "count.gltf").string(), nullptr).meshes.empty());
    fs::remove_all(dir);
}

TEST(LoaderLimits, GltfTriangleLoads) {
    namespace fs = std::filesystem;
    fs::path dir = scratchDir("photon_limits_gltf_ok");
    float verts[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    uint32_t idx[3] = {0, 1, 2};
    writeTriangleGltf(dir, verts, idx);
    auto loaded = GltfLoader::load((dir / "tri.gltf").string(), nullptr);
    fs::remove_all(dir);
    ASSERT_EQ(loaded.meshes.size(), 1u);
    EXPECT_EQ(loaded.meshes[0]->numTriangles(), 1u);
    EXPECT_NEAR(loaded.meshes[0]->positions()[0].x, 0.0f, 1e-5f);
}

TEST(LoaderLimits, ImageCorruptRejectedAndSmallPngLoads) {
    namespace fs = std::filesystem;
    fs::path dir = scratchDir("photon_limits_img");
    writeText(dir / "empty.hdr", "");
    writeText(dir / "empty.png", "");
    writeText(dir / "empty.exr", "");
    writeBytes(dir / "trunc.png", "\x89PNG", 4);
    writeBytes(dir / "trunc.exr", "xxxx", 4);
    std::string big = rleHdr(16385, 1);
    writeBytes(dir / "big.hdr", big.data(), big.size());
    std::string small = rleHdr(1, 1);
    writeBytes(dir / "small.hdr", small.data(), small.size());
    writeBytes(dir / "tiny.png", kPng1x1, sizeof(kPng1x1));

    EXPECT_FALSE(loadImageHDR((dir / "empty.hdr").string()).has_value());
    EXPECT_FALSE(loadImageLDR((dir / "empty.png").string()).has_value());
    EXPECT_FALSE(loadImageEXR((dir / "empty.exr").string()).has_value());
    EXPECT_FALSE(loadImageLDR((dir / "trunc.png").string()).has_value());
    EXPECT_FALSE(loadImageEXR((dir / "trunc.exr").string()).has_value());
    EXPECT_FALSE(loadImageHDR((dir / "big.hdr").string()).has_value());
    EXPECT_FALSE(loadImageLDRMemory(kPng1x1, 4).has_value());

    auto png = loadImageLDR((dir / "tiny.png").string());
    auto pngMem = loadImageLDRMemory(kPng1x1, static_cast<int>(sizeof(kPng1x1)));
    auto hdr = loadImageHDR((dir / "small.hdr").string());
    fs::remove_all(dir);
    ASSERT_TRUE(png.has_value());
    EXPECT_EQ(png->width(), 1);
    EXPECT_EQ(png->height(), 1);
    ASSERT_TRUE(pngMem.has_value());
    EXPECT_EQ(pngMem->width(), 1);
    ASSERT_TRUE(hdr.has_value());
    EXPECT_EQ(hdr->width(), 1);
    EXPECT_EQ(hdr->height(), 1);
}

TEST(LoaderLimits, ProjectEmptyAndTruncatedRejected) {
    namespace fs = std::filesystem;
    fs::path dir = scratchDir("photon_limits_proj");
    writeText(dir / "empty.photon", "");
    writeText(dir / "trunc.photon", "{\"version\": 2");
    writeText(dir / "old.photon", R"({"version":1,"camera":{}})");
    writeText(dir / "badidx.photon",
              R"({"version":2,"nodes":[{"type":"mesh","geometry":{"p":[0,0,0,1,0,0,0,1,0],"i":[0,1,7]}}]})");

    SceneGraph graph;
    ProjectData data;
    std::string err;
    EXPECT_FALSE(loadProject((dir / "empty.photon").string(), graph, data, &err));
    EXPECT_FALSE(loadProject((dir / "trunc.photon").string(), graph, data, &err));
    EXPECT_FALSE(loadProject((dir / "old.photon").string(), graph, data, &err));
    // Out-of-range index: the node is dropped, the project still loads.
    EXPECT_TRUE(loadProject((dir / "badidx.photon").string(), graph, data, &err));
    EXPECT_TRUE(graph.empty());
    fs::remove_all(dir);
}

TEST(Project, RoundTripKeepsSceneLightsAndEnvironment) {
    namespace fs = std::filesystem;
    fs::path dir = scratchDir("photon_proj_roundtrip");
    SceneGraph graph;
    auto mat = std::make_shared<DisneyMaterial>(Color3f(0.1f, 0.2f, 0.3f), 1.0f, 0.25f, 0.5f);
    auto node = std::make_unique<SceneNode>("Üçgen", SceneNodeType::Mesh);
    node->mesh = std::make_shared<TriangleMesh>(
        std::vector<Vec3f>{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, std::vector<Vec3f>{}, std::vector<Vec2f>{},
        std::vector<uint32_t>{0, 1, 2}, mat.get());
    node->material = mat;
    node->localTransform = Transform::translate(Vec3f(1, 2, 3));
    node->visible = false;
    graph.root()->addChild(std::move(node));

    ProjectData data;
    data.environment.rotationDeg = 42.0f;
    data.environment.background.mode = Background::Mode::Transparent;
    LightDesc light;
    light.type = LightDesc::Type::Directional;
    light.intensity = 3.5f;
    data.lights.push_back(light);
    data.camera["fov"] = 30.0;

    const std::string file = pathToUtf8(dir / u8"şişe.photon"); // Türkçe ad: UTF-8 yol testi
    std::string err;
    ASSERT_TRUE(saveProject(file, graph, data, &err)) << err;

    SceneGraph loaded;
    ProjectData back;
    ASSERT_TRUE(loadProject(file, loaded, back, &err)) << err;
    ASSERT_EQ(loaded.root()->children.size(), 1u);
    const SceneNode& n = *loaded.root()->children[0];
    EXPECT_EQ(n.name, "Üçgen");
    EXPECT_FALSE(n.visible);
    EXPECT_NEAR(n.localTransform.matrix()(1, 3), 2.0f, 1e-5f);
    ASSERT_TRUE(n.mesh);
    EXPECT_EQ(n.mesh->numTriangles(), 1u);
    auto d = std::dynamic_pointer_cast<DisneyMaterial>(n.material);
    ASSERT_TRUE(d);
    EXPECT_NEAR(d->roughness(), 0.25f, 1e-5f);
    EXPECT_NEAR(back.environment.rotationDeg, 42.0f, 1e-5f);
    EXPECT_EQ(back.environment.background.mode, Background::Mode::Transparent);
    ASSERT_EQ(back.lights.size(), 1u);
    EXPECT_EQ(back.lights[0].type, LightDesc::Type::Directional);
    EXPECT_NEAR(back.lights[0].intensity, 3.5f, 1e-5f);
    EXPECT_NEAR(back.camera.value("fov", 0.0), 30.0, 1e-9);
    fs::remove_all(dir);
}
