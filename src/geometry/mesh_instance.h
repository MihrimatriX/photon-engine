// mesh_instance.h — Paylaşılan TriangleMesh üzerinde yerel→dünya dönüşümü taşıyan örnek
// (BLAS/TLAS instancing) ve opsiyonel enstantane sonu pozuyla nesne hareket bulanıklığı.
#pragma once

#include "core/math/quat.h"
#include "core/math/transform.h"
#include "geometry/mesh.h"
#include "geometry/shape.h"

#include <cstdint>
#include <memory>

namespace photon {

class MeshInstance : public Shape {
public:
    MeshInstance(std::shared_ptr<const TriangleMesh> mesh, const Transform& localToWorld,
                 const Material* material = nullptr);

    /// Enstantane sonu (t = 1) dönüşümünü ayarlar ve hareketli AABB sınırını günceller.
    void setMotionTransform(const Transform& localToWorld1);

    bool hasMotion() const { return m_hasMotion; }
    const Transform& transform0() const { return m_localToWorld; }
    const Transform& transform1() const { return m_hasMotion ? m_localToWorld1 : m_localToWorld; }

    /// t ∈ [0, 1] anındaki yerel→dünya dönüşümü (konum/ölçek doğrusal, dönme SLERP).
    Transform transformAt(float time) const;

    const std::shared_ptr<const TriangleMesh>& mesh() const { return m_mesh; }
    const Material* material() const { return m_material ? m_material : (m_mesh ? m_mesh->material() : nullptr); }

    bool intersect(Ray& worldRay, SurfaceInteraction& isect) const override;
    bool intersectAny(const Ray& worldRay) const;
    AABB bounds() const override { return m_worldBounds; }

    /// Dışarıdan (Embree TLAS) bulunan üçgen isabetini dünya uzayına dönüştürerek doldurur.
    void shadeInstanceHit(uint32_t primIndex, const Ray& worldRay, float t, float u, float v,
                          SurfaceInteraction& isect) const;

private:
    void recomputeBounds();
    void transformHitToWorld(const Transform& xf, const Ray& worldRay, SurfaceInteraction& isect) const;

    std::shared_ptr<const TriangleMesh> m_mesh;
    Transform m_localToWorld;
    Transform m_localToWorld1;
    bool m_hasMotion = false;
    const Material* m_material = nullptr;
    AABB m_worldBounds;
};

} // namespace photon
