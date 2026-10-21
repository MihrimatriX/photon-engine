// test_import.cpp — Model içe aktarma testleri: glTF düğüm ağacındaki ebeveyn ötelemesinin
// konumlara uygulanması ve OBJ/MTL Kd renginin Lambertian malzemeye atanması.
// Küçük dosyalar geçici klasöre yazılıp yüklenir.
#include <gtest/gtest.h>
#include "io/gltf_loader.h"
#include "io/obj_loader.h"
#include "materials/lambertian.h"
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace photon;

TEST(GltfLoader, ParentTranslationMovesPositions) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "photon_gltf_xform";
    fs::create_directories(dir);
    fs::path binPath = dir / "buf.bin";
    fs::path gltfPath = dir / "box.gltf";

    std::vector<unsigned char> bytes(42, 0);
    float verts[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    std::memcpy(bytes.data(), verts, sizeof(verts));
    uint16_t idx[3] = {0, 1, 2};
    std::memcpy(bytes.data() + 36, idx, sizeof(idx));
    {
        std::ofstream out(binPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    {
        std::ofstream out(gltfPath);
        out << R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [
    {"children": [1], "translation": [1.0, 0.0, 0.0]},
    {"mesh": 0, "translation": [2.0, 0.0, 0.0]}
  ],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 6}
  ],
  "buffers": [{"uri": "buf.bin", "byteLength": 42}]
})";
    }

    auto loaded = GltfLoader::load(gltfPath.string(), nullptr);
    fs::remove_all(dir);
    ASSERT_EQ(loaded.meshes.size(), 1u);
    ASSERT_FALSE(loaded.meshes[0]->positions().empty());
    EXPECT_NEAR(loaded.meshes[0]->positions()[0].x, 3.0f, 1e-3f);
    EXPECT_NEAR(loaded.meshes[0]->positions()[0].y, 0.0f, 1e-3f);
}

TEST(ObjLoader, MtlDiffuseIsAssigned) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "photon_obj_mtl";
    fs::create_directories(dir);
    fs::path objPath = dir / "tri.obj";
    fs::path mtlPath = dir / "tri.mtl";
    {
        std::ofstream mtl(mtlPath);
        mtl << "newmtl red\nKd 0.2 0.8 0.1\n";
    }
    {
        std::ofstream obj(objPath);
        obj << "mtllib tri.mtl\nusemtl red\n";
        obj << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    }
    auto loaded = ObjLoader::load(objPath.string(), nullptr);
    fs::remove_all(dir);
    ASSERT_EQ(loaded.meshes.size(), 1u);
    ASSERT_EQ(loaded.materials.size(), 1u);
    auto* lambert = dynamic_cast<Lambertian*>(loaded.materials[0].get());
    ASSERT_NE(lambert, nullptr);
    EXPECT_NEAR(lambert->albedo().r, 0.2f, 1e-4f);
    EXPECT_NEAR(lambert->albedo().g, 0.8f, 1e-4f);
    EXPECT_NEAR(lambert->albedo().b, 0.1f, 1e-4f);
    EXPECT_EQ(loaded.meshes[0]->numTriangles(), 1u);
}
