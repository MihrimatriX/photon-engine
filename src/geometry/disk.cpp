// disk.cpp — Işın-disk kesişimi: düzlemle kesişim, sonra elips içinde mi testi.
#include "geometry/disk.h"
#include "core/math/constants.h"
#include "core/math/float_bits.h"
#include <algorithm>
#include <cmath>

namespace photon {

Disk::Disk(const Vec3f& center, const Vec3f& ex, const Vec3f& ey, const Material* material)
    : m_center(center), m_ex(ex), m_ey(ey), m_material(material) {
    const Vec3f n = ex.cross(ey);
    m_normal = n.lengthSquared() > 0.0f ? n.normalized() : Vec3f(0, 0, 1);
    const float ex2 = ex.lengthSquared(), ey2 = ey.lengthSquared();
    m_exInv = ex2 > 0.0f ? ex / ex2 : Vec3f(0.0f);
    m_eyInv = ey2 > 0.0f ? ey / ey2 : Vec3f(0.0f);
}

bool Disk::intersect(Ray& ray, SurfaceInteraction& isect) const {
    const float denom = ray.direction.dot(m_normal);
    if (!(std::abs(denom) > 1e-12f)) return false; // düzleme paralel
    const float t = (m_center - ray.origin).dot(m_normal) / denom;
    if (!(t > ray.tMin && t < ray.tMax)) return false;
    const Vec3f q = ray.at(t) - m_center;
    const float s = q.dot(m_exInv), r = q.dot(m_eyInv);
    if (s * s + r * r > 1.0f) return false;

    ray.tMax = t;
    isect.t = t;
    // Nokta düzleme izdüşürülür: o + t·d'nin düzleme dik hatası ışın boyuyla büyür.
    const Vec3f a = m_ex * s, b = m_ey * r;
    isect.point = m_center + a + b;
    const float iso = floatGamma(32) * std::max({m_center.cwiseAbs().maxComponent(), m_ex.length() + m_ey.length(), 1e-20f});
    isect.pError = (m_center.cwiseAbs() + a.cwiseAbs() + b.cwiseAbs()) * floatGamma(16) + Vec3f(iso);
    isect.hasSmoothPoint = false;
    isect.ng = m_normal;
    isect.setFaceNormal(ray, m_normal);
    // uv: kutupsal (açı / 2π, merkezden kenara 0..1).
    float phi = std::atan2(r, s);
    if (phi < 0.0f) phi += TWO_PI;
    isect.uv = Vec2f(phi / TWO_PI, std::sqrt(s * s + r * r));
    isect.tangent = m_ex.lengthSquared() > 0.0f ? m_ex.normalized() : Vec3f(1, 0, 0);
    isect.material = m_material;
    return true;
}

AABB Disk::bounds() const {
    // c + ex·cosθ + ey·sinθ: her eksende uç değer √(ex_i² + ey_i²).
    const Vec3f e(std::sqrt(m_ex.x * m_ex.x + m_ey.x * m_ey.x), std::sqrt(m_ex.y * m_ex.y + m_ey.y * m_ey.y),
                  std::sqrt(m_ex.z * m_ex.z + m_ey.z * m_ey.z));
    return AABB(m_center - e, m_center + e);
}

} // namespace photon
