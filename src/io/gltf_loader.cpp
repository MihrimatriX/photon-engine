#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "io/gltf_loader.h"
#include "materials/disney.h"
#include "materials/lambertian.h"
#include <iostream>
#include <algorithm>

namespace photon {

namespace {

std::shared_ptr<Material> makePbrMaterial(const cgltf_material& mat) {
    if (mat.has_pbr_metallic_roughness) {
        const auto& pbr = mat.pbr_metallic_roughness;
        Color3f base(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]);
        float metallic = pbr.metallic_factor;
        float roughness = std::max(0.001f, pbr.roughness_factor);
        return std::make_shared<DisneyMaterial>(base, metallic, roughness, 0.5f);
    }
    return std::make_shared<Lambertian>(Color3f(0.8f));
}

} // namespace

GltfLoadResult GltfLoader::load(const std::string& path, const Material* defaultMaterial) {
    GltfLoadResult result;
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success) {
        std::cerr << "glTF parse failed: " << path << std::endl;
        return result;
    }
    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) {
        std::cerr << "glTF buffer load failed: " << path << std::endl;
        cgltf_free(data);
        return result;
    }

    for (size_t mi = 0; mi < data->meshes_count; ++mi) {
        const cgltf_mesh& mesh = data->meshes[mi];
        for (size_t pi = 0; pi < mesh.primitives_count; ++pi) {
            const cgltf_primitive& prim = mesh.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;

            const cgltf_accessor* posAcc = nullptr;
            const cgltf_accessor* normAcc = nullptr;
            for (size_t ai = 0; ai < prim.attributes_count; ++ai) {
                if (prim.attributes[ai].type == cgltf_attribute_type_position) posAcc = prim.attributes[ai].data;
                if (prim.attributes[ai].type == cgltf_attribute_type_normal) normAcc = prim.attributes[ai].data;
            }
            if (!posAcc) continue;

            std::shared_ptr<Material> mat;
            if (prim.material) mat = makePbrMaterial(*prim.material);
            else if (defaultMaterial)
                mat = std::shared_ptr<Material>(const_cast<Material*>(defaultMaterial), [](Material*) {});
            else
                mat = std::make_shared<Lambertian>(Color3f(0.8f));

            std::vector<Vec3f> positions;
            std::vector<Vec3f> normals;
            std::vector<Vec2f> uvs;
            std::vector<uint32_t> indices;

            auto readVec3 = [&](const cgltf_accessor* acc, size_t i) {
                float v[3] = {};
                cgltf_accessor_read_float(acc, i, v, 3);
                return Vec3f(v[0], v[1], v[2]);
            };

            if (prim.indices) {
                for (size_t i = 0; i < prim.indices->count; ++i) {
                    cgltf_size vi = cgltf_accessor_read_index(prim.indices, i);
                    positions.push_back(readVec3(posAcc, vi));
                    if (normAcc) normals.push_back(readVec3(normAcc, vi));
                    indices.push_back(static_cast<uint32_t>(i));
                }
            } else {
                for (size_t i = 0; i < posAcc->count; ++i) {
                    positions.push_back(readVec3(posAcc, i));
                    if (normAcc) normals.push_back(readVec3(normAcc, i));
                    indices.push_back(static_cast<uint32_t>(i));
                }
            }
            if (positions.empty()) continue;

            result.materials.push_back(mat);
            result.meshes.push_back(std::make_shared<TriangleMesh>(
                positions, normals, uvs, indices, result.materials.back().get()));
        }
    }

    cgltf_free(data);
    return result;
}

} // namespace photon
