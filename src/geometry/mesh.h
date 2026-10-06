// mesh.h — Üçgen ağı (TriangleMesh): köşe konumları, normaller, UV'ler ve indeks tamponu.
// BVH ve Embree tek tek üçgenlere (index) erişir; yığında ayrı Triangle nesnesi tutulmaz.
#pragma once

/// @file mesh.h
/// @brief TriangleMesh class storing vertex buffers and generating individual triangles in PhotonEngine.

#include "geometry/shape.h"
#include "geometry/triangle.h"
#include <vector>
#include <memory>
#include <cstdint>

namespace photon {

/// @brief Mesh primitive containing vertex and index buffers.
class TriangleMesh : public Shape {
public:
    TriangleMesh(const std::vector<Vec3f>& positions,
                 const std::vector<Vec3f>& normals,
                 const std::vector<Vec2f>& uvs,
                 const std::vector<uint32_t>& indices,
                 const Material* material);

    bool intersect(Ray& ray, SurfaceInteraction& isect) const override;
    AABB bounds() const override;

    size_t numTriangles() const { return m_indices.size() / 3; }

    /// Stack triangle, no heap shape. BVH leaves index these.
    bool intersectTriangle(size_t index, Ray& ray, SurfaceInteraction& isect) const;
    /// Kesişim dışarıda (Embree) bulunduysa: t ve barisentriklerle yüzey verisini doldur.
    void shadeTriangle(size_t index, const Ray& ray, float t, float u, float v, SurfaceInteraction& isect) const;
    AABB triangleBounds(size_t index) const;
    float triangleArea(size_t index) const;
    void triangleVertices(size_t index, Vec3f& p0, Vec3f& p1, Vec3f& p2) const;

    /// Get individual triangle shape for acceleration structures
    std::shared_ptr<Triangle> getTriangle(size_t index) const;

    const std::vector<Vec3f>& positions() const { return m_positions; }
    const std::vector<Vec3f>& normals() const { return m_normals; }
    const std::vector<Vec2f>& uvs() const { return m_uvs; }
    const std::vector<uint32_t>& indices() const { return m_indices; }
    const Material* material() const { return m_material; }

private:
    std::vector<Vec3f> m_positions;
    std::vector<Vec3f> m_normals;
    std::vector<Vec2f> m_uvs;
    std::vector<uint32_t> m_indices;
    const Material* m_material;
    
    AABB m_bounds;

    void calculateNormals();
};

} // namespace photon
