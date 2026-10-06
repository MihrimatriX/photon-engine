// mesh.cpp — TriangleMesh: sınırlar, eksik normallerin hesaplanması, tek üçgen kesişimi
// ve dışarıda (Embree) bulunan isabet için yüzey verisinin doldurulması.
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
        m_bounds.merge(p);
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

void TriangleMesh::triangleVertices(size_t index, Vec3f& p0, Vec3f& p1, Vec3f& p2) const {
    size_t offset = index * 3;
    p0 = m_positions[m_indices[offset]];
    p1 = m_positions[m_indices[offset + 1]];
    p2 = m_positions[m_indices[offset + 2]];
}

float TriangleMesh::triangleArea(size_t index) const {
    Vec3f p0, p1, p2;
    triangleVertices(index, p0, p1, p2);
    return 0.5f * (p1 - p0).cross(p2 - p0).length();
}

AABB TriangleMesh::triangleBounds(size_t index) const {
    Vec3f p0, p1, p2;
    triangleVertices(index, p0, p1, p2);
    AABB box;
    box.pMin = p0.cwiseMin(p1).cwiseMin(p2);
    box.pMax = p0.cwiseMax(p1).cwiseMax(p2);
    return box;
}

bool TriangleMesh::intersectTriangle(size_t index, Ray& ray, SurfaceInteraction& isect) const {
    size_t offset = index * 3;
    uint32_t idx0 = m_indices[offset];
    uint32_t idx1 = m_indices[offset + 1];
    uint32_t idx2 = m_indices[offset + 2];

    bool hasN = m_normals.size() == m_positions.size();
    bool hasUV = m_uvs.size() == m_positions.size();
    Vec3f n0 = hasN ? m_normals[idx0] : Vec3f(0.0f);
    Vec3f n1 = hasN ? m_normals[idx1] : Vec3f(0.0f);
    Vec3f n2 = hasN ? m_normals[idx2] : Vec3f(0.0f);
    Vec2f uv0 = hasUV ? m_uvs[idx0] : Vec2f(0.0f);
    Vec2f uv1 = hasUV ? m_uvs[idx1] : Vec2f(0.0f);
    Vec2f uv2 = hasUV ? m_uvs[idx2] : Vec2f(0.0f);

    Triangle tri(m_positions[idx0], m_positions[idx1], m_positions[idx2],
                 n0, n1, n2, uv0, uv1, uv2, m_material);
    return tri.intersect(ray, isect);
}

void TriangleMesh::shadeTriangle(size_t index, const Ray& ray, float t, float u, float v,
                                 SurfaceInteraction& isect) const {
    // Yığında geçici üçgen (getTriangle heap ayırır; isabet başına ayırma pahalı).
    const size_t o = index * 3;
    const uint32_t i0 = m_indices[o], i1 = m_indices[o + 1], i2 = m_indices[o + 2];
    const bool hasN = m_normals.size() == m_positions.size();
    const bool hasUV = m_uvs.size() == m_positions.size();
    Triangle tri(m_positions[i0], m_positions[i1], m_positions[i2],
                 hasN ? m_normals[i0] : Vec3f(0.0f), hasN ? m_normals[i1] : Vec3f(0.0f), hasN ? m_normals[i2] : Vec3f(0.0f),
                 hasUV ? m_uvs[i0] : Vec2f(0.0f), hasUV ? m_uvs[i1] : Vec2f(0.0f), hasUV ? m_uvs[i2] : Vec2f(0.0f),
                 m_material);
    tri.fillHit(ray, t, u, v, isect);
}

std::shared_ptr<Triangle> TriangleMesh::getTriangle(size_t index) const {
    size_t offset = index * 3;
    uint32_t idx0 = m_indices[offset];
    uint32_t idx1 = m_indices[offset + 1];
    uint32_t idx2 = m_indices[offset + 2];

    bool hasN = m_normals.size() == m_positions.size();
    bool hasUV = m_uvs.size() == m_positions.size();
    Vec3f n0 = hasN ? m_normals[idx0] : Vec3f(0.0f);
    Vec3f n1 = hasN ? m_normals[idx1] : Vec3f(0.0f);
    Vec3f n2 = hasN ? m_normals[idx2] : Vec3f(0.0f);

    Vec2f uv0 = hasUV ? m_uvs[idx0] : Vec2f(0.0f);
    Vec2f uv1 = hasUV ? m_uvs[idx1] : Vec2f(0.0f);
    Vec2f uv2 = hasUV ? m_uvs[idx2] : Vec2f(0.0f);

    return std::make_shared<Triangle>(
        m_positions[idx0], m_positions[idx1], m_positions[idx2],
        n0, n1, n2,
        uv0, uv1, uv2,
        m_material
    );
}

bool TriangleMesh::intersect(Ray& ray, SurfaceInteraction& isect) const {
    bool hit = false;
    size_t numTris = numTriangles();
    for (size_t i = 0; i < numTris; ++i) {
        if (intersectTriangle(i, ray, isect)) hit = true;
    }
    return hit;
}

AABB TriangleMesh::bounds() const {
    return m_bounds;
}

} // namespace photon
