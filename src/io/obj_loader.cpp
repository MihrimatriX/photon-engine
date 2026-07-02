#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "io/obj_loader.h"
#include <iostream>

namespace photon {

std::vector<std::shared_ptr<TriangleMesh>> ObjLoader::load(
    const std::string& path,
    const Material* defaultMaterial) {
    
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    // Load OBJ geometry
    // Note: we don't load materials automatically for simplicity, just assigning defaultMaterial
    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str());

    if (!warn.empty()) {
        std::clog << "OBJ Loader Warning: " << warn << std::endl;
    }
    if (!err.empty()) {
        std::cerr << "OBJ Loader Error: " << err << std::endl;
    }

    if (!ret) {
        return {};
    }

    std::vector<std::shared_ptr<TriangleMesh>> meshes;

    for (const auto& shape : shapes) {
        std::vector<Vec3f> positions;
        std::vector<Vec3f> normals;
        std::vector<Vec2f> uvs;
        std::vector<uint32_t> indices;

        size_t index_offset = 0;
        // Loop over faces (polygons)
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            size_t fv = size_t(shape.mesh.num_face_vertices[f]);

            // We only support triangle faces. If the face has 4 vertices (quad),
            // we skip or split. But for simplicity, assume it's triangulated.
            if (fv != 3) {
                index_offset += fv;
                continue;
            }

            for (size_t v = 0; v < 3; v++) {
                tinyobj::index_t idx = shape.mesh.indices[index_offset + v];

                // Position
                Vec3f pos(
                    attrib.vertices[3 * size_t(idx.vertex_index) + 0],
                    attrib.vertices[3 * size_t(idx.vertex_index) + 1],
                    attrib.vertices[3 * size_t(idx.vertex_index) + 2]
                );
                positions.push_back(pos);

                // Normal
                if (idx.normal_index >= 0) {
                    Vec3f norm(
                        attrib.normals[3 * size_t(idx.normal_index) + 0],
                        attrib.normals[3 * size_t(idx.normal_index) + 1],
                        attrib.normals[3 * size_t(idx.normal_index) + 2]
                    );
                    normals.push_back(norm);
                }

                // UV
                if (idx.texcoord_index >= 0) {
                    Vec2f uv(
                        attrib.texcoords[2 * size_t(idx.texcoord_index) + 0],
                        attrib.texcoords[2 * size_t(idx.texcoord_index) + 1]
                    );
                    uvs.push_back(uv);
                }

                indices.push_back(static_cast<uint32_t>(indices.size()));
            }
            index_offset += 3;
        }

        if (!positions.empty()) {
            meshes.push_back(std::make_shared<TriangleMesh>(
                positions, normals, uvs, indices, defaultMaterial
            ));
        }
    }

    return meshes;
}

} // namespace photon
