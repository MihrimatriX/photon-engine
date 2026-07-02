#include "geometry/mesh.h"
#include <iostream>

namespace photon {

TriangleMesh::TriangleMesh(const std::vector<Vec3f>& positions,
                           const std::vector<Vec3f>& normals,
                           const std::vector<Vec2f>& uvs,
                           const std::vector<uint32_t>& indices,
                           const Material* material)
    : m_positions(positions), m_normals(normals), m_uvs(uvs), m_indices(indices), m_material(material) {
    
    if (m_normals.empty() && !m_positions.empty() && !m_indices.empty()) {
        calculateNormals();
    }

    // Compute bounding box
    m_bounds = AABB::empty();
    for (const auto& p : m_positions) {
        m_bounds = m_bounds.merge(p);
    }
}

void TriangleMesh::calculateNormals() {
    m_normals.resize(m_positions.size(), Vec3f(0.0f));
    for (size_t i = 0; i < m_indices.size(); i += 3) {
        uint32_t idx0 = m_indices[i];
        uint32_t idx1 = m_indices[i + 1];
        uint32_t idx2 = m_indices[i + 2];

        Vec3f v0 = m_positions[idx0];
        Vec3f v1 = m_positions[idx1];
        Vec3f v2 = m_positions[idx2];

        Vec3f n = (v1 - v0).cross(v2 - v0); // Unnormalized face normal

        m_normals[idx0] += n;
        m_normals[idx1] += n;
        m_normals[idx2] += n;
    }

    for (auto& n : m_normals) {
        if (n.lengthSquared() > 0.0f) {
            n = n.normalized();
        }
    }
}

std::shared_ptr<Triangle> TriangleMesh::getTriangle(size_t index) const {
    size_t offset = index * 3;
    uint32_t idx0 = m_indices[offset];
    uint32_t idx1 = m_indices[offset + 1];
    uint32_t idx2 = m_indices[offset + 2];

    Vec3f n0 = m_normals.empty() ? Vec3f(0.0f) : m_normals[idx0];
    Vec3f n1 = m_normals.empty() ? Vec3f(0.0f) : m_normals[idx1];
    Vec3f n2 = m_normals.empty() ? Vec3f(0.0f) : m_normals[idx2];

    Vec2f uv0 = m_uvs.empty() ? Vec2f(0.0f) : m_uvs[idx0];
    Vec2f uv1 = m_uvs.empty() ? Vec2f(0.0f) : m_uvs[idx1];
    Vec2f uv2 = m_uvs.empty() ? Vec2f(0.0f) : m_uvs[idx2];

    return std::make_shared<Triangle>(
        m_positions[idx0], m_positions[idx1], m_positions[idx2],
        n0, n1, n2,
        uv0, uv1, uv2,
        m_material
    );
}

bool TriangleMesh::intersect(Ray& ray, SurfaceInteraction& isect) const {
    // Brute force intersection (only used as fallback, normally BVH is used)
    bool hit = false;
    size_t numTris = numTriangles();
    for (size_t i = 0; i < numTris; ++i) {
        auto tri = getTriangle(i);
        if (tri->intersect(ray, isect)) {
            hit = true;
        }
    }
    return hit;
}

AABB TriangleMesh::bounds() const {
    return m_bounds;
}

} // namespace photon
