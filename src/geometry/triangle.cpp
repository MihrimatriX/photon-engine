// Tek üçgen ışın kesişimi (Möller–Trumbore 1997) ve kesişim noktasında normal, UV,
// teğet enterpolasyonu. Mesh dışındaki bağımsız üçgenler (ör. alan ışığı quad'ları) bunu kullanır.

#include "geometry/triangle.h"
#include "core/math/float_bits.h"
#include <algorithm>
#include <cmath>

namespace photon {

bool Triangle::intersect(Ray& ray, SurfaceInteraction& isect) const {
    Vec3f edge1 = m_v1 - m_v0;
    Vec3f edge2 = m_v2 - m_v0;
    Vec3f h = ray.direction.cross(edge2);
    float a = edge1.dot(h);

    // Determinant a = -(e1 × e2)·d = -|e1||e2| sinφ cosθ (φ: kenarlar arası açı,
    // θ: ışın ile normal arası açı). Mutlak 1e-6 eşiği alanı ~1e-6'dan küçük üçgenleri
    // (ör. 1 mm'lik model) tamamen görünmez yapıyordu (geometry-1). Eşiği |e1||e2|'ye
    // göre GÖRELİ alırız: yalnızca ışın düzleme gerçekten paralelse (|cosθ| ≲ 1e-7) reddet.
    // Kareler kullanıldığından karekök gerekmez.
    if (a * a <= 1e-14f * edge1.lengthSquared() * edge2.lengthSquared()) {
        return false; // Ray is parallel to the triangle (or the triangle is degenerate)
    }

    float f = 1.0f / a;
    Vec3f s = ray.origin - m_v0;
    float u = f * s.dot(h);

    // Karşılaştırmalar NaN'da da reddedecek biçimde yazıldı (!(x >= 0) NaN için true).
    if (!(u >= 0.0f && u <= 1.0f)) {
        return false;
    }

    Vec3f q = s.cross(edge1);
    float v = f * ray.direction.dot(q);

    if (!(v >= 0.0f && u + v <= 1.0f)) {
        return false;
    }

    float t = f * edge2.dot(q);

    if (!(t >= ray.tMin && t <= ray.tMax)) {
        return false;
    }

    // Hit confirmed!
    ray.tMax = t; // Update ray's tMax
    fillHit(ray, t, u, v, isect);
    return true;
}

void Triangle::fillHit(const Ray& ray, float t, float u, float v, SurfaceInteraction& isect) const {
    const Vec3f edge1 = m_v1 - m_v0;
    const Vec3f edge2 = m_v2 - m_v0;
    isect.t = t;

    // Compute barycentric weights
    float w = 1.0f - u - v;

    // Nokta barisentriklerden (o + t·d değil): hatası yalnız köşe büyüklüklerine bağlıdır, ışının
    // ne kadar yol aldığına değil. Sınır PBRT-v4 Triangle ile aynı biçim, γ(7) yerine fast-math
    // (FMA, yeniden sıralama) payıyla γ(16). Ek izotrop terim (köşelerin en büyük bileşeni ×
    // γ(32)): PBRT kesişim testinin kendisinde t > 0'ı muhafazakâr denetler; Möller–Trumbore'un
    // t hatası köşe büyüklüğüyle orantılıdır ve bunu ofset karşılar. Yoksa y = 0 düzlemindeki
    // bir noktanın y hatası tam 0 çıkar, ışın kendi düzlemine t = 0'da çarpar.
    const Vec3f a0 = m_v0 * w, a1 = m_v1 * u, a2 = m_v2 * v;
    isect.point = a0 + a1 + a2;
    const float iso = floatGamma(32) * std::max({m_v0.cwiseAbs().maxComponent(), m_v1.cwiseAbs().maxComponent(),
                                                 m_v2.cwiseAbs().maxComponent(), 1e-20f});
    isect.pError = (a0.cwiseAbs() + a1.cwiseAbs() + a2.cwiseAbs()) * floatGamma(16) + Vec3f(iso);

    // Geometric normal (cross product of edges)
    Vec3f geomNormal = edge1.cross(edge2).normalized();

    // Shading normal (interpolate if available)
    Vec3f shadingNormal;
    if (m_n0.lengthSquared() > 0.0f && m_n1.lengthSquared() > 0.0f && m_n2.lengthSquared() > 0.0f) {
        shadingNormal = (m_n0 * w + m_n1 * u + m_n2 * v).normalized();
    } else {
        shadingNormal = geomNormal;
    }

    isect.ng = geomNormal;
    isect.setFaceNormal(ray, shadingNormal);

    // Gölge terminatörü (Hanika 2021, "Hacking the Shadow Terminator", RTG II böl. 4): noktanın
    // her köşenin teğet düzlemine olan uzaklığı, o köşenin altında kalıyorsa yukarı itilir ve
    // barisentrik ağırlıkla toplanır: p' = p + Σ bᵢ · max(0, (vᵢ − p)·nᵢ) · nᵢ. Bu, köşe
    // normallerinin tarif ettiği yumuşak yüzeye (Phong tessellation gibi) bir yaklaşımdır.
    // Düz yüzde (nᵢ = ng) etkisizdir.
    isect.hasSmoothPoint = false;
    if (m_n0.lengthSquared() > 0.0f && m_n1.lengthSquared() > 0.0f && m_n2.lengthSquared() > 0.0f) {
        const Vec3f& p = isect.point;
        const Vec3f n0 = m_n0.normalized(), n1 = m_n1.normalized(), n2 = m_n2.normalized();
        const Vec3f lift = n0 * (w * std::max(0.0f, (m_v0 - p).dot(n0))) +
                           n1 * (u * std::max(0.0f, (m_v1 - p).dot(n1))) +
                           n2 * (v * std::max(0.0f, (m_v2 - p).dot(n2)));
        if (lift.lengthSquared() > 0.0f) {
            isect.pSmooth = p + lift;
            isect.hasSmoothPoint = true;
        }
    }

    // Interpolate UV coordinates
    isect.uv = m_uv0 * w + m_uv1 * u + m_uv2 * v;

    // Calculate tangent (direction of increasing U)
    // dP = dU * T + dV * B
    // We solve for T and B
    Vec2f deltaUV1 = m_uv1 - m_uv0;
    Vec2f deltaUV2 = m_uv2 - m_uv0;

    float r = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
    if (std::abs(r) < 1e-6f) {
        // Fallback tangent
        float sign = std::copysign(1.0f, isect.normal.z);
        float a_coeff = -1.0f / (sign + isect.normal.z);
        float b_coeff = isect.normal.x * isect.normal.y * a_coeff;
        isect.tangent = Vec3f(1.0f + sign * isect.normal.x * isect.normal.x * a_coeff, sign * b_coeff, -sign * isect.normal.x);
    } else {
        float invR = 1.0f / r;
        isect.tangent = (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * invR;
    }
    isect.tangent = isect.tangent.normalized();

    isect.material = m_material;
}

AABB Triangle::bounds() const {
    AABB box;
    box.pMin = m_v0.cwiseMin(m_v1).cwiseMin(m_v2);
    box.pMax = m_v0.cwiseMax(m_v1).cwiseMax(m_v2);
    return box;
}

} // namespace photon
