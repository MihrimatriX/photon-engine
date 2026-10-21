// obj_loader.cpp — tinyobjloader üzerine güvenli OBJ okuyucu.
// Dosya güvenilmez girdi sayılır: boyut sınırları, indeks doğrulaması ve sonlu sayı
// (NaN/Inf olmayan) kontrolü burada yapılır; bozuk dosya yarım değil BOŞ sonuç döner.
// Çokgenler üçgen yelpazesine bölünür, köşeler malzeme kimliğine göre kovalara ayrılır.
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "io/obj_loader.h"
#include "core/math/float_bits.h"
#include "materials/lambertian.h"
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace photon {
namespace {

// ponytail: ceiling — 512MB per input, 50M source verts, 50M triangles.
// A bigger mesh needs a streaming loader, not a larger alloc.
constexpr uintmax_t kMaxObjFileBytes = 512ull * 1024ull * 1024ull;
constexpr size_t kMaxObjVertices = 50'000'000;
constexpr size_t kMaxObjTriangles = 50'000'000;
constexpr size_t kMaxObjCorners = kMaxObjTriangles * 3;

bool regularFileWithin(const std::filesystem::path& p, uintmax_t cap) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec) || ec) return false;
    auto sz = std::filesystem::file_size(p, ec);
    return !ec && sz <= cap;
}

// tinyobjloader MTL dosyasını kendisi açar. Bu yüzden "mtllib" satırları önceden taranır ve
// başvurulan her MTL'nin de normal dosya ve boyut sınırı içinde olduğu doğrulanır.
bool objInputsOk(const std::filesystem::path& objPath) {
    if (!regularFileWithin(objPath, kMaxObjFileBytes)) return false;
    std::ifstream in(objPath);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        size_t i = 0;
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i + 6 > line.size() || line.compare(i, 6, "mtllib") != 0) continue;
        size_t j = i + 6;
        if (j >= line.size() || !std::isspace(static_cast<unsigned char>(line[j]))) continue;
        while (j < line.size()) {
            while (j < line.size() && std::isspace(static_cast<unsigned char>(line[j]))) ++j;
            if (j >= line.size()) break;
            size_t k = j;
            while (k < line.size() && !std::isspace(static_cast<unsigned char>(line[k]))) ++k;
            std::filesystem::path mtl(line.substr(j, k - j));
            if (mtl.is_relative()) mtl = objPath.parent_path() / mtl;
            std::error_code ec;
            if (std::filesystem::exists(mtl, ec) && !regularFileWithin(mtl, kMaxObjFileBytes)) return false;
            j = k;
        }
    }
    return true;
}

// İndeks → dizi elemanı erişimi güvenli mi: negatif değil, sınırın altında ve
// [index·comps, index·comps + comps) aralığı kaynak dizinin içinde. (tinyobj OBJ'nin göreli
// negatif indekslerini zaten mutlağa çevirir; burada negatif = eksik/bozuk demektir.)
bool indexElemOk(int index, size_t cap, int comps, size_t srcSize) {
    if (index < 0 || comps <= 0) return false;
    size_t i = static_cast<size_t>(index);
    if (i >= cap) return false;
    size_t base = i * static_cast<size_t>(comps);
    return base + static_cast<size_t>(comps) <= srcSize;
}

} // namespace

ObjLoadResult ObjLoader::load(const std::string& path, const Material* defaultMaterial) {
    if (!objInputsOk(path)) {
        std::cerr << "OBJ Loader Error: missing or oversized file: " << path << std::endl;
        return {};
    }

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    std::string baseDir = std::filesystem::path(path).parent_path().string();
    // triangulate=false so n-gons reach the fan below instead of being pre-split and the fv!=3 drop.
    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str(),
                                baseDir.empty() ? nullptr : baseDir.c_str(), false);

    if (!warn.empty()) {
        std::clog << "OBJ Loader Warning: " << warn << std::endl;
    }
    if (!err.empty()) {
        std::cerr << "OBJ Loader Error: " << err << std::endl;
    }

    ObjLoadResult result;
    if (!ret) return result;

    if (attrib.vertices.size() / 3 > kMaxObjVertices
        || attrib.normals.size() / 3 > kMaxObjVertices
        || attrib.texcoords.size() / 2 > kMaxObjVertices) {
        std::cerr << "OBJ Loader Error: vertex count exceeds limit" << std::endl;
        return {};
    }

    size_t triCount = 0;
    size_t cornerCount = 0;

    for (const auto& shape : shapes) {
        // Bir OBJ şekli birden çok "usemtl" içerebilir. Her malzeme kimliği için ayrı bir kova
        // (konum/normal/uv/indeks) tutulur; sonunda her kova ayrı bir TriangleMesh olur.
        struct Bucket {
            int matId = -1;
            std::vector<Vec3f> positions;
            std::vector<Vec3f> normals;
            std::vector<Vec2f> uvs;
            std::vector<uint32_t> indices;
        };
        std::vector<Bucket> buckets;
        auto bucketFor = [&](int matId) -> Bucket& {
            for (auto& b : buckets) {
                if (b.matId == matId) return b;
            }
            buckets.push_back(Bucket{matId, {}, {}, {}, {}});
            return buckets.back();
        };

        size_t index_offset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            size_t fv = size_t(shape.mesh.num_face_vertices[f]);
            if (index_offset > shape.mesh.indices.size()
                || fv > shape.mesh.indices.size() - index_offset) {
                std::cerr << "OBJ Loader Error: face index out of range" << std::endl;
                return {};
            }
            if (fv < 3) {
                index_offset += fv;
                continue;
            }
            // n köşeli yüz yelpazeyle n-2 üçgen verir. Sınır "a > max - b" biçiminde, yani
            // toplamadan ÖNCE çıkarmayla denetlenir; böylece size_t taşması hiç oluşmaz.
            size_t addTris = fv - 2;
            if (addTris > kMaxObjTriangles || triCount > kMaxObjTriangles - addTris
                || addTris > kMaxObjCorners / 3
                || cornerCount > kMaxObjCorners - addTris * 3) {
                std::cerr << "OBJ Loader Error: triangle count exceeds limit" << std::endl;
                return {};
            }
            triCount += addTris;
            cornerCount += addTris * 3;

            int matId = (f < shape.mesh.material_ids.size()) ? shape.mesh.material_ids[f] : -1;
            Bucket& bucket = bucketFor(matId);
            auto pushVert = [&](const tinyobj::index_t& idx) -> bool {
                if (!indexElemOk(idx.vertex_index, kMaxObjVertices, 3, attrib.vertices.size()))
                    return false;
                size_t vi = static_cast<size_t>(idx.vertex_index);
                float x = attrib.vertices[3 * vi + 0];
                float y = attrib.vertices[3 * vi + 1];
                float z = attrib.vertices[3 * vi + 2];
                if (!finite3(x, y, z)) return false;
                bucket.positions.push_back(Vec3f(x, y, z));
                // Normal yoksa dizi boş kalır; TriangleMesh o zaman normalleri üçgenlerden kendisi
                // hesaplar. Köşeler paylaşılmadığı için sonuç düz (faceted) gölgelemedir.
                if (idx.normal_index >= 0) {
                    if (!indexElemOk(idx.normal_index, kMaxObjVertices, 3, attrib.normals.size()))
                        return false;
                    size_t ni = static_cast<size_t>(idx.normal_index);
                    float nx = attrib.normals[3 * ni + 0];
                    float ny = attrib.normals[3 * ni + 1];
                    float nz = attrib.normals[3 * ni + 2];
                    if (!finite3(nx, ny, nz)) return false;
                    bucket.normals.push_back(Vec3f(nx, ny, nz));
                }
                if (idx.texcoord_index >= 0) {
                    if (!indexElemOk(idx.texcoord_index, kMaxObjVertices, 2, attrib.texcoords.size()))
                        return false;
                    size_t ti = static_cast<size_t>(idx.texcoord_index);
                    float u = attrib.texcoords[2 * ti + 0];
                    float v = attrib.texcoords[2 * ti + 1];
                    if (!finiteFloat(u) || !finiteFloat(v)) return false;
                    // OBJ puts v = 0 at the bottom of the image; Image rows start at the top.
                    // (glTF'te v=0 zaten görüntünün üstüdür; gltf_loader bu yüzden v'yi çevirmez.)
                    bucket.uvs.push_back(Vec2f(u, 1.0f - v));
                }
                // Köşeler kaynaklanmaz (weld yok), her köşe ayrı kopyalanır: OBJ'de konum/normal/uv
                // indeksleri birbirinden bağımsız olduğu için en basit doğru yol bu. Bedeli bellek.
                bucket.indices.push_back(static_cast<uint32_t>(bucket.indices.size()));
                return true;
            };
            // ponytail: fan from vertex 0. Concave faces need an ear clip, not this.
            for (size_t i = 1; i + 1 < fv; ++i) {
                if (index_offset + i + 1 >= shape.mesh.indices.size()) return {};
                if (!pushVert(shape.mesh.indices[index_offset])
                    || !pushVert(shape.mesh.indices[index_offset + i])
                    || !pushVert(shape.mesh.indices[index_offset + i + 1])) {
                    std::cerr << "OBJ Loader Error: index out of range or non-finite position" << std::endl;
                    return {};
                }
            }
            index_offset += fv;
        }

        // MTL'den yalnız Kd okunur → Lambertian. Ks/Ns/map_Kd vb. şimdilik yok sayılır.
        // owned boşsa mesh çağıranın varsayılan malzemesini kullanır (materials, meshes'e paralel).
        for (auto& bucket : buckets) {
            if (bucket.positions.empty()) continue;
            std::shared_ptr<Material> owned;
            const Material* matPtr = defaultMaterial;
            if (bucket.matId >= 0 && bucket.matId < static_cast<int>(materials.size())) {
                const auto& mtl = materials[static_cast<size_t>(bucket.matId)];
                owned = std::make_shared<Lambertian>(Color3f(mtl.diffuse[0], mtl.diffuse[1], mtl.diffuse[2]));
                matPtr = owned.get();
            }
            result.materials.push_back(owned);
            result.meshes.push_back(std::make_shared<TriangleMesh>(
                bucket.positions, bucket.normals, bucket.uvs, bucket.indices, matPtr));
        }
    }

    return result;
}

} // namespace photon
