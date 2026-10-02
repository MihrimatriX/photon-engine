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
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <unordered_set>

namespace photon {

namespace {

// ponytail: ceiling — 512MB per file and summed buffer byteLengths, 50M verts,
// 50M tris, 1M nodes/meshes, node depth 256. Bigger scenes need streaming.
constexpr uintmax_t kMaxGltfFileBytes = 512ull * 1024ull * 1024ull;
constexpr size_t kMaxGltfVertices = 50'000'000;
constexpr size_t kMaxGltfTriangles = 50'000'000;
constexpr size_t kMaxGltfCorners = kMaxGltfTriangles * 3;
constexpr size_t kMaxGltfNodes = 1'000'000;
constexpr size_t kMaxGltfMeshes = 1'000'000;
constexpr size_t kMaxGltfAccessors = 2'000'000;
constexpr size_t kMaxGltfAttributes = 32;
constexpr int kMaxGltfNodeDepth = 256;

bool fileWithin(const std::filesystem::path& p, uintmax_t cap) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(p, ec);
    return !ec && sz <= cap;
}

bool nodeOwned(const cgltf_data* data, const cgltf_node* node) {
    return data && data->nodes && node
        && node >= data->nodes && node < data->nodes + data->nodes_count;
}

bool meshOwned(const cgltf_data* data, const cgltf_mesh* mesh) {
    return data && data->meshes && mesh
        && mesh >= data->meshes && mesh < data->meshes + data->meshes_count;
}

bool parentChainFinite(const cgltf_data* data, const cgltf_node* node) {
    const cgltf_node* p = node;
    for (int steps = 0; p; ++steps) {
        if (steps > kMaxGltfNodeDepth || !nodeOwned(data, p)) return false;
        p = p->parent;
    }
    return true;
}

bool finiteFloat(float x) {
    uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}

bool finite3(float x, float y, float z) {
    return finiteFloat(x) && finiteFloat(y) && finiteFloat(z);
}

uint64_t accessorElemBytes(const cgltf_accessor* acc) {
    int comps = 0;
    switch (acc->type) {
    case cgltf_type_scalar: comps = 1; break;
    case cgltf_type_vec2: comps = 2; break;
    case cgltf_type_vec3: comps = 3; break;
    case cgltf_type_vec4: case cgltf_type_mat2: comps = 4; break;
    case cgltf_type_mat3: comps = 9; break;
    case cgltf_type_mat4: comps = 16; break;
    default: return 0;
    }
    int compBytes = 0;
    switch (acc->component_type) {
    case cgltf_component_type_r_8: case cgltf_component_type_r_8u: compBytes = 1; break;
    case cgltf_component_type_r_16: case cgltf_component_type_r_16u: compBytes = 2; break;
    case cgltf_component_type_r_32u: case cgltf_component_type_r_32f: compBytes = 4; break;
    default: return 0;
    }
    return static_cast<uint64_t>(comps) * static_cast<uint64_t>(compBytes);
}

bool accessorBytesFit(const cgltf_accessor* acc) {
    if (!acc || acc->count > kMaxGltfCorners) return false;
    if (!acc->buffer_view) return true;
    const cgltf_buffer_view* view = acc->buffer_view;
    if (!view->buffer || !view->buffer->data) return false;
    if (view->offset > view->buffer->size) return false;
    if (view->size > view->buffer->size - view->offset) return false;
    if (acc->count == 0) return true;
    uint64_t elem = accessorElemBytes(acc);
    if (elem == 0 || acc->stride < elem || acc->offset > view->size) return false;
    if (acc->count > 1 && acc->stride > view->size) return false;
    uint64_t last = static_cast<uint64_t>(acc->offset)
        + static_cast<uint64_t>(acc->stride) * static_cast<uint64_t>(acc->count - 1) + elem;
    return last >= static_cast<uint64_t>(acc->offset) && last <= view->size;
}

bool countsOk(const cgltf_data* data) {
    if (!data) return false;
    if (data->nodes_count > kMaxGltfNodes || (data->nodes_count && !data->nodes)) return false;
    if (data->meshes_count > kMaxGltfMeshes || (data->meshes_count && !data->meshes)) return false;
    if (data->accessors_count > kMaxGltfAccessors || (data->accessors_count && !data->accessors)) return false;
    if (data->buffer_views_count > kMaxGltfAccessors) return false;
    if (data->buffers_count > kMaxGltfMeshes || (data->buffers_count && !data->buffers)) return false;
    if (data->images_count > kMaxGltfMeshes) return false;
    if (data->materials_count > kMaxGltfMeshes) return false;
    if (data->textures_count > kMaxGltfMeshes) return false;
    if (data->scenes_count > kMaxGltfNodes) return false;
    if (data->animations_count > kMaxGltfMeshes) return false;
    if (data->skins_count > kMaxGltfMeshes) return false;

    for (cgltf_size i = 0; i < data->accessors_count; ++i) {
        if (data->accessors[i].count > kMaxGltfCorners) return false;
    }
    for (cgltf_size i = 0; i < data->meshes_count; ++i) {
        const cgltf_mesh& mesh = data->meshes[i];
        if (mesh.primitives_count > kMaxGltfMeshes) return false;
        if (mesh.primitives_count && !mesh.primitives) return false;
        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            if (mesh.primitives[p].attributes_count > kMaxGltfAttributes) return false;
        }
    }
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        if (data->nodes[i].children_count > data->nodes_count) return false;
        if (data->nodes[i].children_count && !data->nodes[i].children) return false;
    }
    if (data->scene && data->scene->nodes_count > data->nodes_count) return false;
    return true;
}

bool buffersWithinLimit(const cgltf_data* data, const std::filesystem::path& baseDir) {
    uint64_t total = 0;
    for (cgltf_size i = 0; i < data->buffers_count; ++i) {
        const cgltf_buffer& buf = data->buffers[i];
        if (buf.size > kMaxGltfFileBytes) return false;
        if (total > kMaxGltfFileBytes - buf.size) return false;
        total += buf.size;
        if (!buf.uri || std::strncmp(buf.uri, "data:", 5) == 0) continue;
        std::error_code ec;
        auto sz = std::filesystem::file_size(baseDir / buf.uri, ec);
        if (!ec && sz > kMaxGltfFileBytes) return false;
    }
    return true;
}

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
        const cgltf_buffer* buf = view->buffer;
        if (view->offset <= buf->size && view->size <= buf->size - view->offset
            && view->size > 0 && view->size <= kMaxGltfFileBytes
            && view->size <= static_cast<cgltf_size>(std::numeric_limits<int>::max())) {
            const auto* bytes = static_cast<const unsigned char*>(buf->data) + view->offset;
            loaded = loadImageLDRMemory(bytes, static_cast<int>(view->size));
        }
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

bool appendPrimitive(GltfLoadResult& result, const cgltf_primitive& prim, const Transform& world,
                     const Material* defaultMaterial, const std::filesystem::path& baseDir,
                     size_t& corners) {
    if (prim.type != cgltf_primitive_type_triangles) return true;
    if (prim.attributes_count > kMaxGltfAttributes) return false;
    if (prim.attributes_count && !prim.attributes) return false;

    const cgltf_accessor* posAcc = nullptr;
    const cgltf_accessor* normAcc = nullptr;
    const cgltf_accessor* uvAcc = nullptr;
    for (size_t ai = 0; ai < prim.attributes_count; ++ai) {
        const cgltf_attribute& attr = prim.attributes[ai];
        if (!attr.data) continue;
        if (attr.type == cgltf_attribute_type_position) posAcc = attr.data;
        else if (attr.type == cgltf_attribute_type_normal) normAcc = attr.data;
        else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) uvAcc = attr.data;
    }
    if (!posAcc) return true;

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

    auto readVec3 = [&](const cgltf_accessor* acc, size_t i, Vec3f& out) -> bool {
        float v[3] = {};
        if (!cgltf_accessor_read_float(acc, i, v, 3)) return false;
        if (!finite3(v[0], v[1], v[2])) return false;
        out = world.transformPoint(Vec3f(v[0], v[1], v[2]));
        return finite3(out.x, out.y, out.z);
    };
    auto readNormal = [&](const cgltf_accessor* acc, size_t i, Vec3f& out) -> bool {
        float v[3] = {};
        if (!cgltf_accessor_read_float(acc, i, v, 3)) return false;
        if (!finite3(v[0], v[1], v[2])) return false;
        Vec3f n = world.transformNormal(Vec3f(v[0], v[1], v[2]));
        out = n.lengthSquared() > 0.0f ? n.normalized() : n;
        return finite3(out.x, out.y, out.z);
    };
    auto readVec2 = [&](const cgltf_accessor* acc, size_t i, Vec2f& out) -> bool {
        float v[2] = {};
        if (!cgltf_accessor_read_float(acc, i, v, 2)) return false;
        if (!finiteFloat(v[0]) || !finiteFloat(v[1])) return false;
        out = Vec2f(v[0], v[1]);
        return true;
    };

    if (posAcc->count > kMaxGltfVertices) return false;
    const cgltf_size n = prim.indices ? prim.indices->count : posAcc->count;
    if (n > kMaxGltfCorners || corners > kMaxGltfCorners - static_cast<size_t>(n)) return false;
    if (!accessorBytesFit(posAcc)) return false;
    if (normAcc && !accessorBytesFit(normAcc)) return false;
    if (uvAcc && !accessorBytesFit(uvAcc)) return false;

    auto pushCorner = [&](cgltf_size vi, uint32_t slot) -> bool {
        if (vi >= posAcc->count) return false;
        if (normAcc && vi >= normAcc->count) return false;
        if (uvAcc && vi >= uvAcc->count) return false;
        Vec3f p;
        if (!readVec3(posAcc, vi, p)) return false;
        positions.push_back(p);
        if (normAcc) {
            Vec3f nrm;
            if (!readNormal(normAcc, vi, nrm)) return false;
            normals.push_back(nrm);
        }
        if (uvAcc) {
            Vec2f uv;
            if (!readVec2(uvAcc, vi, uv)) return false;
            uvs.push_back(uv);
        }
        indices.push_back(slot);
        return true;
    };

    if (prim.indices) {
        const auto comp = prim.indices->component_type;
        if (comp != cgltf_component_type_r_8u && comp != cgltf_component_type_r_16u
            && comp != cgltf_component_type_r_32u) return false;
        if (!accessorBytesFit(prim.indices)) return false;
        for (cgltf_size i = 0; i < prim.indices->count; ++i) {
            if (i > static_cast<cgltf_size>(std::numeric_limits<uint32_t>::max())) return false;
            cgltf_size vi = cgltf_accessor_read_index(prim.indices, i);
            if (!pushCorner(vi, static_cast<uint32_t>(i))) return false;
        }
    } else {
        for (cgltf_size i = 0; i < posAcc->count; ++i) {
            if (i > static_cast<cgltf_size>(std::numeric_limits<uint32_t>::max())) return false;
            if (!pushCorner(i, static_cast<uint32_t>(i))) return false;
        }
    }
    if (positions.empty()) return true;

    corners += n;
    result.materials.push_back(mat);
    result.meshes.push_back(std::make_shared<TriangleMesh>(
        positions, normals, uvs, indices, result.materials.back().get()));
    return true;
}

bool walkNode(const cgltf_data* data, GltfLoadResult& result, cgltf_node* node,
              const Material* defaultMaterial, const std::filesystem::path& baseDir,
              int depth, std::unordered_set<const cgltf_node*>& seen, size_t& corners) {
    if (!nodeOwned(data, node)) return false;
    if (depth > kMaxGltfNodeDepth) return false;
    if (!parentChainFinite(data, node)) return false;
    if (!seen.insert(node).second) return false;
    if (node->mesh) {
        if (!meshOwned(data, node->mesh)) return false;
        if (node->mesh->primitives_count > kMaxGltfMeshes) return false;
        if (node->mesh->primitives_count && !node->mesh->primitives) return false;
        cgltf_float wm[16];
        cgltf_node_transform_world(node, wm);
        Transform world(fromCgltfColumnMajor(wm));
        for (size_t pi = 0; pi < node->mesh->primitives_count; ++pi) {
            if (!appendPrimitive(result, node->mesh->primitives[pi], world, defaultMaterial, baseDir, corners))
                return false;
        }
    }
    if (node->children_count > data->nodes_count) return false;
    if (node->children_count && !node->children) return false;
    for (size_t i = 0; i < node->children_count; ++i) {
        if (!walkNode(data, result, node->children[i], defaultMaterial, baseDir, depth + 1, seen, corners))
            return false;
    }
    return true;
}

} // namespace

GltfLoadResult GltfLoader::load(const std::string& path, const Material* defaultMaterial) {
    GltfLoadResult result;
    if (!fileWithin(path, kMaxGltfFileBytes)) {
        std::cerr << "glTF file missing or too large: " << path << std::endl;
        return result;
    }

    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success) {
        std::cerr << "glTF parse failed: " << path << std::endl;
        return result;
    }

    std::filesystem::path baseDir = std::filesystem::path(path).parent_path();
    if (!countsOk(data) || !buffersWithinLimit(data, baseDir)) {
        std::cerr << "glTF exceeds limits: " << path << std::endl;
        cgltf_free(data);
        return result;
    }
    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) {
        std::cerr << "glTF buffer load failed: " << path << std::endl;
        cgltf_free(data);
        return result;
    }

    bool ok = true;
    size_t corners = 0;
    std::unordered_set<const cgltf_node*> seen;
    if (data->scene) {
        if (data->scene->nodes_count && !data->scene->nodes) ok = false;
        for (size_t i = 0; ok && i < data->scene->nodes_count; ++i) {
            ok = walkNode(data, result, data->scene->nodes[i], defaultMaterial, baseDir, 1, seen, corners);
        }
    } else {
        for (size_t i = 0; ok && i < data->nodes_count; ++i) {
            if (data->nodes[i].parent == nullptr)
                ok = walkNode(data, result, &data->nodes[i], defaultMaterial, baseDir, 1, seen, corners);
        }
    }

    if (ok && result.meshes.empty()) {
        for (size_t mi = 0; ok && mi < data->meshes_count; ++mi) {
            const cgltf_mesh& mesh = data->meshes[mi];
            if (mesh.primitives_count && !mesh.primitives) { ok = false; break; }
            for (size_t pi = 0; pi < mesh.primitives_count; ++pi) {
                if (!appendPrimitive(result, mesh.primitives[pi], Transform{}, defaultMaterial, baseDir, corners)) {
                    ok = false;
                    break;
                }
            }
        }
    }

    cgltf_free(data);
    if (!ok) return {};
    return result;
}

} // namespace photon
