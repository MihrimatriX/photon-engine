#include <gtest/gtest.h>
#include "core/image/image.h"
#include "core/image/image_io.h"
#include "io/gltf_loader.h"
#include "io/obj_loader.h"
#include "scene/project_io.h"
#include "scene/scene_graph.h"
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
    writeText(dir / "trunc.photon", "{\"version\": 1");
    writeText(dir / "ok.photon", R"({"version":1,"camera":{},"imports":["tri.obj"],"includeCornell":false})");

    SceneGraph graph;
    std::string json;
    EXPECT_FALSE(loadProject((dir / "empty.photon").string(), graph, json));
    EXPECT_TRUE(loadProject((dir / "trunc.photon").string(), graph, json));
    ProjectFile proj;
    EXPECT_FALSE(parseProjectJson(json, proj));
    EXPECT_FALSE(parseProjectJson("{", proj));
    EXPECT_TRUE(loadProject((dir / "ok.photon").string(), graph, json));
    ASSERT_TRUE(parseProjectJson(json, proj));
    ASSERT_EQ(proj.imports.size(), 1u);
    EXPECT_EQ(proj.imports[0].path, "tri.obj");
    fs::remove_all(dir);
}
