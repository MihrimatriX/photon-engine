// cylinder.cpp — Işın-silindir kesişimi: eksene dik düzlemde ikinci derece denklem, sonra
// eksen boyunca boy sınırı.
#include "geometry/cylinder.h"
#include "core/math/constants.h"
#include "core/math/float_bits.h"
#include "core/math/frame.h"
#include "core/math/utils.h"
#include <algorithm>
#include <cmath>

namespace photon {

Cylinder::Cylinder(const Vec3f& center, const Vec3f& halfAxis, float radius, const Material* material)
    : m_center(center), m_radius(std::max(0.0f, radius)), m_material(material) {
    m_halfLength = halfAxis.length();
    m_axis = m_halfLength > 0.0f ? halfAxis / m_halfLength : Vec3f(1, 0, 0);
    const Frame f(m_axis);
    m_b = f.s;
    m_c = f.t;
}

bool Cylinder::intersect(Ray& ray, SurfaceInteraction& isect) const {
    if (!(m_radius > 0.0f) || !(m_halfLength > 0.0f)) return false;
    // Eksene dik bileşenler: |o⊥ + t·d⊥|² = r².
    const Vec3f o = ray.origin - m_center;
    const float oz = o.dot(m_axis), dz = ray.direction.dot(m_axis);
    const Vec3f op = o - m_axis * oz, dp = ray.direction - m_axis * dz;
    const float a = dp.dot(dp);
    if (!(a > 1e-12f * ray.direction.lengthSquared())) return false; // eksene paralel ışın yan yüzeye çarpmaz
    // Kararlı ayırtaç (küre ile aynı): 4a(r² − |f|²), f ışının eksene en yakın noktasının dik bileşeni.
    const float b = 2.0f * op.dot(dp), c = op.dot(op) - m_radius * m_radius;
    const Vec3f f = op - dp * (0.5f * b / a);
    const float fl = f.length();
    const float disc = 4.0f * a * (m_radius + fl) * (m_radius - fl);
    if (disc < 0.0f) return false;
    const float rootDisc = std::sqrt(disc);
    const float q = -0.5f * (b < 0.0f ? b - rootDisc : b + rootDisc);
    float t0 = q / a;
    float t1 = q != 0.0f ? c / q : t0;
    if (t0 > t1) std::swap(t0, t1);
    float tHit = 0.0f;
    bool found = false;
    for (const float t : {t0, t1}) {
        if (!(t > ray.tMin && t < ray.tMax)) continue;
        if (std::abs(oz + t * dz) > m_halfLength) continue;
        tHit = t;
        found = true;
        break;
    }
    if (!found) return false;

    ray.tMax = tHit;
    isect.t = tHit;
    const float z = oz + tHit * dz;
    Vec3f radial = op + dp * tHit;
    const float rl = radial.length();
    const Vec3f n = rl > 0.0f ? radial / rl : Vec3f(0, 1, 0);
    // Nokta silindir yüzeyine yeniden izdüşürülür (küre ile aynı gerekçe).
    const Vec3f along = m_axis * z, out = n * m_radius;
    isect.point = m_center + along + out;
    const float iso = floatGamma(32) * std::max({m_center.cwiseAbs().maxComponent(), m_halfLength + m_radius, 1e-20f});
    isect.pError = (m_center.cwiseAbs() + along.cwiseAbs() + out.cwiseAbs()) * floatGamma(16) + Vec3f(iso);
    isect.hasSmoothPoint = false;
    isect.ng = n;
    isect.setFaceNormal(ray, n);
    // uv: eksen etrafındaki açı / 2π, boy boyunca 0..1. Teğet çevre yönünde (eksen × normal).
    const Vec3f t = m_axis.cross(n);
    float phi = std::atan2(n.dot(m_c), n.dot(m_b));
    if (phi < 0.0f) phi += TWO_PI;
    isect.uv = Vec2f(phi / TWO_PI, 0.5f * (z / m_halfLength + 1.0f));
    isect.tangent = t.lengthSquared() > 0.0f ? t.normalized() : Vec3f(1, 0, 0);
    isect.material = m_material;
    return true;
}

AABB Cylinder::bounds() const {
    // Uç çemberlerin her eksendeki uzanımı r·√(1 − a_i²).
    const Vec3f a = m_axis.cwiseAbs();
    const Vec3f e(a.x * m_halfLength + m_radius * std::sqrt(std::max(0.0f, 1.0f - a.x * a.x)),
                  a.y * m_halfLength + m_radius * std::sqrt(std::max(0.0f, 1.0f - a.y * a.y)),
                  a.z * m_halfLength + m_radius * std::sqrt(std::max(0.0f, 1.0f - a.z * a.z)));
    return AABB(m_center - e, m_center + e);
}

} // namespace photon
