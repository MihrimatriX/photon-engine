#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "io/obj_loader.h"
#include "materials/lambertian.h"
#include <filesystem>
#include <iostream>

namespace photon {

ObjLoadResult ObjLoader::load(const std::string& path, const Material* defaultMaterial) {
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

    for (const auto& shape : shapes) {
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
            if (fv < 3) {
                index_offset += fv;
                continue;
            }
            int matId = (f < shape.mesh.material_ids.size()) ? shape.mesh.material_ids[f] : -1;
            Bucket& bucket = bucketFor(matId);
            auto pushVert = [&](const tinyobj::index_t& idx) {
                bucket.positions.push_back(Vec3f(
                    attrib.vertices[3 * size_t(idx.vertex_index) + 0],
                    attrib.vertices[3 * size_t(idx.vertex_index) + 1],
                    attrib.vertices[3 * size_t(idx.vertex_index) + 2]));
                if (idx.normal_index >= 0) {
                    bucket.normals.push_back(Vec3f(
                        attrib.normals[3 * size_t(idx.normal_index) + 0],
                        attrib.normals[3 * size_t(idx.normal_index) + 1],
                        attrib.normals[3 * size_t(idx.normal_index) + 2]));
                }
                if (idx.texcoord_index >= 0) {
                    bucket.uvs.push_back(Vec2f(
                        attrib.texcoords[2 * size_t(idx.texcoord_index) + 0],
                        attrib.texcoords[2 * size_t(idx.texcoord_index) + 1]));
                }
                bucket.indices.push_back(static_cast<uint32_t>(bucket.indices.size()));
            };
            // ponytail: fan from vertex 0. Concave faces need an ear clip, not this.
            for (size_t i = 1; i + 1 < fv; ++i) {
                pushVert(shape.mesh.indices[index_offset]);
                pushVert(shape.mesh.indices[index_offset + i]);
                pushVert(shape.mesh.indices[index_offset + i + 1]);
            }
            index_offset += fv;
        }

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
