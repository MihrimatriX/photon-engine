#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "io/gltf_loader.h"
#include "materials/disney.h"
#include "materials/lambertian.h"
#include "core/image/image_io.h"
#include "core/math/transform.h"
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <optional>

namespace photon {

namespace {

Mat4f fromCgltfColumnMajor(const cgltf_float* cm) {
    Mat4f m;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            m(row, col) = cm[col * 4 + row];
        }
    }
    return m;
}

void tryBaseColor(DisneyMaterial& disney, const cgltf_material& mat, const std::filesystem::path& baseDir) {
    if (!mat.has_pbr_metallic_roughness) return;
    const cgltf_texture* tex = mat.pbr_metallic_roughness.base_color_texture.texture;
    if (!tex || !tex->image) return;
    const cgltf_image* image = tex->image;

    std::optional<Image> loaded;
    if (image->uri && std::strncmp(image->uri, "data:", 5) != 0) {
        std::string uri = image->uri;
        auto slash = uri.find_last_of('.');
        std::string ext = slash == std::string::npos ? "" : uri.substr(slash);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        // stb already decodes these. KTX/Basis and other GPU formats are skipped.
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") {
            loaded = loadImageLDR((baseDir / uri).string());
        }
    }
    if (!loaded && image->buffer_view && image->buffer_view->buffer && image->buffer_view->buffer->data) {
        const cgltf_buffer_view* view = image->buffer_view;
        const auto* bytes = static_cast<const unsigned char*>(view->buffer->data) + view->offset;
        loaded = loadImageLDRMemory(bytes, static_cast<int>(view->size));
    }
    if (loaded) disney.setAlbedoImage(std::make_shared<Image>(std::move(*loaded)));
}

std::shared_ptr<Material> makePbrMaterial(const cgltf_material& mat, const std::filesystem::path& baseDir) {
    if (mat.has_pbr_metallic_roughness) {
        const auto& pbr = mat.pbr_metallic_roughness;
        Color3f base(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]);
        float metallic = pbr.metallic_factor;
        float roughness = std::max(0.001f, pbr.roughness_factor);
        auto disney = std::make_shared<DisneyMaterial>(base, metallic, roughness, 0.5f);
        tryBaseColor(*disney, mat, baseDir);
        return disney;
    }
    return std::make_shared<Lambertian>(Color3f(0.8f));
}

void appendPrimitive(GltfLoadResult& result, const cgltf_primitive& prim, const Transform& world,
                     const Material* defaultMaterial, const std::filesystem::path& baseDir) {
    if (prim.type != cgltf_primitive_type_triangles) return;

    const cgltf_accessor* posAcc = nullptr;
    const cgltf_accessor* normAcc = nullptr;
    const cgltf_accessor* uvAcc = nullptr;
    for (size_t ai = 0; ai < prim.attributes_count; ++ai) {
        const cgltf_attribute& attr = prim.attributes[ai];
        if (attr.type == cgltf_attribute_type_position) posAcc = attr.data;
        else if (attr.type == cgltf_attribute_type_normal) normAcc = attr.data;
        else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) uvAcc = attr.data;
    }
    if (!posAcc) return;

    std::shared_ptr<Material> mat;
    if (prim.material) mat = makePbrMaterial(*prim.material, baseDir);
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
        return world.transformPoint(Vec3f(v[0], v[1], v[2]));
    };
    auto readNormal = [&](const cgltf_accessor* acc, size_t i) {
        float v[3] = {};
        cgltf_accessor_read_float(acc, i, v, 3);
        Vec3f n = world.transformNormal(Vec3f(v[0], v[1], v[2]));
        return n.lengthSquared() > 0.0f ? n.normalized() : n;
    };
    auto readVec2 = [&](const cgltf_accessor* acc, size_t i) {
        float v[2] = {};
        cgltf_accessor_read_float(acc, i, v, 2);
        return Vec2f(v[0], v[1]);
    };

    if (prim.indices) {
        for (size_t i = 0; i < prim.indices->count; ++i) {
            cgltf_size vi = cgltf_accessor_read_index(prim.indices, i);
            positions.push_back(readVec3(posAcc, vi));
            if (normAcc) normals.push_back(readNormal(normAcc, vi));
            if (uvAcc) uvs.push_back(readVec2(uvAcc, vi));
            indices.push_back(static_cast<uint32_t>(i));
        }
    } else {
        for (size_t i = 0; i < posAcc->count; ++i) {
            positions.push_back(readVec3(posAcc, i));
            if (normAcc) normals.push_back(readNormal(normAcc, i));
            if (uvAcc) uvs.push_back(readVec2(uvAcc, i));
            indices.push_back(static_cast<uint32_t>(i));
        }
    }
    if (positions.empty()) return;

    result.materials.push_back(mat);
    result.meshes.push_back(std::make_shared<TriangleMesh>(
        positions, normals, uvs, indices, result.materials.back().get()));
}

void walkNode(GltfLoadResult& result, cgltf_node* node, const Material* defaultMaterial,
              const std::filesystem::path& baseDir) {
    if (!node) return;
    if (node->mesh) {
        cgltf_float wm[16];
        cgltf_node_transform_world(node, wm);
        Transform world(fromCgltfColumnMajor(wm));
        for (size_t pi = 0; pi < node->mesh->primitives_count; ++pi) {
            appendPrimitive(result, node->mesh->primitives[pi], world, defaultMaterial, baseDir);
        }
    }
    for (size_t i = 0; i < node->children_count; ++i) {
        walkNode(result, node->children[i], defaultMaterial, baseDir);
    }
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

    std::filesystem::path baseDir = std::filesystem::path(path).parent_path();
    if (data->scene) {
        for (size_t i = 0; i < data->scene->nodes_count; ++i)
            walkNode(result, data->scene->nodes[i], defaultMaterial, baseDir);
    } else {
        for (size_t i = 0; i < data->nodes_count; ++i) {
            if (data->nodes[i].parent == nullptr)
                walkNode(result, &data->nodes[i], defaultMaterial, baseDir);
        }
    }

    if (result.meshes.empty()) {
        for (size_t mi = 0; mi < data->meshes_count; ++mi) {
            const cgltf_mesh& mesh = data->meshes[mi];
            for (size_t pi = 0; pi < mesh.primitives_count; ++pi)
                appendPrimitive(result, mesh.primitives[pi], Transform{}, defaultMaterial, baseDir);
        }
    }

    cgltf_free(data);
    return result;
}

} // namespace photon
