// mesh_instance.cpp — MeshInstance kesişim, dünya uzayı normal/teğet/pError dönüşümü ve hareket sınırları.
#include "geometry/mesh_instance.h"

#include <algorithm>
#include <cmath>

namespace photon {

namespace {

float det3x3(const Mat4f& m) {
    return m(0, 0) * (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1)) -
           m(0, 1) * (m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0)) +
           m(0, 2) * (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0));
}

} // namespace

MeshInstance::MeshInstance(std::shared_ptr<const TriangleMesh> mesh, const Transform& localToWorld,
                           const Material* material)
    : m_mesh(std::move(mesh)), m_localToWorld(localToWorld), m_localToWorld1(localToWorld), m_material(material) {
    recomputeBounds();
}

void MeshInstance::setMotionTransform(const Transform& localToWorld1) {
    m_localToWorld1 = localToWorld1;
    m_hasMotion = true;
    recomputeBounds();
}

Transform MeshInstance::transformAt(float time) const {
    if (!m_hasMotion) return m_localToWorld;
    return interpolateTransform(m_localToWorld, m_localToWorld1, time);
}

void MeshInstance::recomputeBounds() {
    if (!m_mesh) {
        m_worldBounds = AABB::empty();
        return;
    }
    const AABB localBox = m_mesh->bounds();
    if (!m_hasMotion) {
        m_worldBounds = m_localToWorld.transformAABB(localBox);
        return;
    }
    // SLERP dönme yayının dışarı taşmaması için enstantane boyunca 5 örnekte birleşik kutu.
    AABB merged = AABB::empty();
    constexpr int kSteps = 4;
    for (int i = 0; i <= kSteps; ++i) {
        const float a = static_cast<float>(i) / static_cast<float>(kSteps);
        merged.merge(interpolateTransform(m_localToWorld, m_localToWorld1, a).transformAABB(localBox));
    }
    m_worldBounds = merged;
}

void MeshInstance::transformHitToWorld(const Transform& xf, const Ray& worldRay, SurfaceInteraction& isect) const {
    const Mat4f& m = xf.matrix();
    const float det = det3x3(m);

    // Yerel outward normali kurtar: (M⁻¹)ᵀ hem pozitif hem negatif determinantta dışa bakan normali korur.
    const Vec3f localOutNg = isect.ng;
    const Vec3f localOutNs = isect.frontFace ? isect.normal : -isect.normal;

    isect.point = xf.transformPoint(isect.point);
    if (isect.hasSmoothPoint) isect.pSmooth = xf.transformPoint(isect.pSmooth);

    Vec3f ngW = xf.transformNormal(localOutNg);
    if (ngW.lengthSquared() > 0.0f) ngW = ngW.normalized();
    Vec3f nsW = xf.transformNormal(localOutNs);
    if (nsW.lengthSquared() > 0.0f) nsW = nsW.normalized();
    else nsW = ngW;

    isect.ng = ngW;
    isect.setFaceNormal(worldRay, nsW);

    Vec3f tW = xf.transformVector(isect.tangent);
    tW = tW - isect.normal * isect.normal.dot(tW);
    if (tW.lengthSquared() > 1e-12f) {
        isect.tangent = tW.normalized();
    } else {
        const float sign = std::copysign(1.0f, isect.normal.z);
        const float a = -1.0f / (sign + isect.normal.z);
        const float b = isect.normal.x * isect.normal.y * a;
        isect.tangent = Vec3f(1.0f + sign * isect.normal.x * isect.normal.x * a, sign * b, -sign * isect.normal.x).normalized();
    }

    // Dünya uzayı hata kutusu (ölçekli/dönmüş örneklerde kendi kendine kesişim olmasın).
    const Vec3f absR0(std::abs(m(0, 0)), std::abs(m(0, 1)), std::abs(m(0, 2)));
    const Vec3f absR1(std::abs(m(1, 0)), std::abs(m(1, 1)), std::abs(m(1, 2)));
    const Vec3f absR2(std::abs(m(2, 0)), std::abs(m(2, 1)), std::abs(m(2, 2)));
    const Vec3f pAbs(std::abs(isect.point.x), std::abs(isect.point.y), std::abs(isect.point.z));
    isect.pError = Vec3f(absR0.dot(isect.pError), absR1.dot(isect.pError), absR2.dot(isect.pError)) +
                   pAbs * 1e-5f + Vec3f(1e-6f);

    if (isect.uvDensity > 0.0f) {
        const float linScale = std::cbrt(std::max(std::abs(det), 1e-12f));
        isect.uvDensity /= linScale;
    }

    isect.material = material();
    isect.hitObject = this;
}

bool MeshInstance::intersect(Ray& worldRay, SurfaceInteraction& isect) const {
    if (!m_mesh) return false;
    if ((visibilityMask() & worldRay.visibilityMask) == 0) return false;
    const Transform xf = transformAt(worldRay.time);
    Ray localRay = xf.inverse().transformRay(worldRay);
    localRay.visibilityMask = VisAll;
    if (!m_mesh->intersectAccelerated(localRay, isect)) return false;
    worldRay.tMax = localRay.tMax;
    transformHitToWorld(xf, worldRay, isect);
    return true;
}

bool MeshInstance::intersectAny(const Ray& worldRay) const {
    if (!m_mesh) return false;
    if ((visibilityMask() & worldRay.visibilityMask) == 0) return false;
    const Transform xf = transformAt(worldRay.time);
    Ray localRay = xf.inverse().transformRay(worldRay);
    localRay.visibilityMask = VisAll;
    return m_mesh->intersectAnyAccelerated(localRay);
}

void MeshInstance::shadeInstanceHit(uint32_t primIndex, const Ray& worldRay, float t, float u, float v,
                                    SurfaceInteraction& isect) const {
    if (!m_mesh) return;
    const Transform xf = transformAt(worldRay.time);
    const Ray localRay = xf.inverse().transformRay(worldRay);
    m_mesh->shadeTriangle(primIndex, localRay, t, u, v, isect);
    transformHitToWorld(xf, worldRay, isect);
}

} // namespace photon
