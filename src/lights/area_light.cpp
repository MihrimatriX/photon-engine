// area_light.cpp — Dikdörtgen ve mesh alan ışıklarının örneklenmesi ve pdf'i.
#include "lights/area_light.h"
#include "geometry/mesh.h"
#include "core/math/constants.h"
#include "core/sampling/sampling.h"
#include <algorithm>
#include <cmath>

namespace photon {

namespace {

// Alan pdf'i → katı açı pdf'i (PBRT-v4 §4.2.3, alan üzerinden integraller).
// Işıktaki dA yüzeyi alıcıdan dω = |cos θ_l| · dA / r² katı açısıyla görünür. Yüzeyde düzgün
// örneklemede p_A = 1/A, dolayısıyla p_ω = p_A · dA/dω = r² / (|cos θ_l| · A).
// Kenardan bakışta (cos → 0) pdf patlar; böyle örnekler 0 döner (katkıları zaten ~0).
float areaSolidAnglePdf(float distSq, float cosAbs, float area) {
    if (!(cosAbs > 1e-6f) || !(area > 0.0f) || !(distSq > 0.0f)) return 0.0f;
    return distSq / (area * cosAbs);
}

} // namespace

LightSample AreaLight::sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const {
    LightSample ls;
    
    // Sample point on the light source rectangle
    // p = köşe + s·u + t·v, (s, t) ∈ [0,1)² düzgün → dikdörtgende alan-düzgün dağılım (p_A = 1/A).
    Vec3f pLight = m_position + m_u * sample.x + m_v * sample.y;
    Vec3f toLight = pLight - si.point;
    
    float distSq = toLight.lengthSquared();
    ls.distance = std::sqrt(distSq);
    
    if (ls.distance > 0.0f) {
        ls.wi = toLight / ls.distance;

        // ponytail: two-sided; one-sided needs an explicit flag later
        float cosThetaL = std::abs(ls.wi.dot(m_normal));
        float pdf = areaSolidAnglePdf(distSq, cosThetaL, m_area);

        if (pdf > 0.0f) {
            ls.Li = m_radiance;
            // Area PDF = 1 / Area
            // Convert to solid angle PDF: PDF_solid = PDF_area * dist^2 / |cos|
            ls.pdf = pdf;
        } else {
            ls.Li = Color3f::black();
            ls.pdf = 0.0f;
        }
    }
    
    return ls;
}

// pdfLi: BSDF örneklemesi ışığa çarptığında, sampleLi'nin o noktayı hangi pdf ile üreteceği
// (MIS ağırlığı için gerekli). Önce nokta düzlemde mi, sonra (s, t) yerel koordinatları [0,1]
// içinde mi diye bakılır: d = s·u + t·v eşitliği u ve v ile skaler çarpılınca 2×2 sistem
// [uu uv; uv vv]·[s t]ᵀ = [du dv]ᵀ çıkar, Cramer kuralıyla çözülür (u ⟂ v olmasa da çalışır).
float AreaLight::pdfLi(const Vec3f& ref, const Vec3f& pLight) const {
    Vec3f d = pLight - m_position;
    float scale = std::max(m_u.length() + m_v.length(), 1.0f);
    if (std::abs(d.dot(m_normal)) > 1e-3f * scale) return 0.0f;

    float uu = m_u.dot(m_u);
    float vv = m_v.dot(m_v);
    float uv = m_u.dot(m_v);
    float du = d.dot(m_u);
    float dv = d.dot(m_v);
    float det = uu * vv - uv * uv;
    if (std::abs(det) <= 1e-12f) return 0.0f;

    float s = (vv * du - uv * dv) / det;
    float t = (uu * dv - uv * du) / det;
    const float eps = 1e-3f;
    if (s < -eps || t < -eps || s > 1.0f + eps || t > 1.0f + eps) return 0.0f;

    Vec3f toLight = pLight - ref;
    float distSq = toLight.lengthSquared();
    if (distSq <= 0.0f) return 0.0f;
    Vec3f wi = toLight / std::sqrt(distSq);
    return areaSolidAnglePdf(distSq, std::abs(wi.dot(m_normal)), m_area);
}

Color3f AreaLight::power() const {
    // Lambert yayıcı: Φ = L · A · π (yarıküre üzerinde ∫ cos θ dω = π). Tek yüz için.
    return m_radiance * m_area * PI;
}

// Üçgen seçimi için alan CDF'i: büyük üçgen orantılı olarak daha sık seçilir.
// P(üçgen i) = A_i / A, üçgen içinde düzgün örnekleme p = 1/A_i → birleşik alan pdf'i
// (A_i / A) · (1 / A_i) = 1/A, yani tüm mesh yüzeyinde düzgün dağılım.
MeshLight::MeshLight(const TriangleMesh* mesh, const Color3f& radiance)
    : m_mesh(mesh), m_radiance(radiance) {
    if (!m_mesh) return;
    size_t n = m_mesh->numTriangles();
    m_cdf.resize(n);
    float sum = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        sum += m_mesh->triangleArea(i);
        m_cdf[i] = sum;
    }
    m_area = sum;
}

LightSample MeshLight::sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const {
    LightSample ls;
    if (!m_mesh || m_area <= 0.0f || m_radiance.isBlack() || m_cdf.empty()) return ls;

    // sample.x iki kez kullanılır: önce CDF'te ikili aramayla üçgeni seçer, sonra seçilen aralık
    // içindeki konumu u0 = (pick - prev) / triArea ile yeniden [0,1)'e ölçeklenir. Yeni rastgele
    // sayı harcamadan örnek dağılımı korunur. 0.999 kırpması pick'in m_area'ya eşit olup son
    // elemanı aşmasını önler.
    float pick = std::clamp(sample.x, 0.0f, 0.999f) * m_area;
    auto it = std::lower_bound(m_cdf.begin(), m_cdf.end(), pick);
    size_t tri = static_cast<size_t>(std::min(it, m_cdf.end() - 1) - m_cdf.begin());
    float prev = tri == 0 ? 0.0f : m_cdf[tri - 1];
    float triArea = std::max(m_cdf[tri] - prev, 1e-12f);
    float u0 = (pick - prev) / triArea;

    Vec3f p0, p1, p2;
    m_mesh->triangleVertices(tri, p0, p1, p2);
    // Üçgende düzgün örnek (PBRT-v3 §13.6.5): b = (1 - √u, v·√u). Karekök olmadan noktalar
    // bir köşede yığılırdı. Barisentrik ağırlıklarla p = b0·p0 + b1·p1 + b2·p2.
    Vec2f b = uniformSampleTriangle(Vec2f(std::clamp(u0, 0.0f, 1.0f), sample.y));
    float b0 = 1.0f - b.x - b.y;
    Vec3f pLight = p0 * b0 + p1 * b.x + p2 * b.y;
    Vec3f n = (p1 - p0).cross(p2 - p0);
    float len = n.length();
    if (len <= 1e-12f) return ls;
    n = n / len;

    Vec3f toLight = pLight - si.point;
    float distSq = toLight.lengthSquared();
    ls.distance = std::sqrt(distSq);
    if (ls.distance <= 0.0f) return ls;
    ls.wi = toLight / ls.distance;
    float pdf = areaSolidAnglePdf(distSq, std::abs(ls.wi.dot(n)), m_area);
    if (pdf <= 0.0f) return ls;
    ls.Li = m_radiance;
    ls.pdf = pdf;
    return ls;
}

float MeshLight::pdfLi(const Vec3f& ref, const Vec3f& pLight) const {
    // ponytail: linear scan to recover which triangle owns the hit. Upgrade = store the tri id on the isect.
    // Noktanın hangi üçgende olduğu bulunur: düzleme uzaklık eşiği, sonra barisentrik koordinatlar
    // (Gram matrisi + Cramer, AreaLight::pdfLi ile aynı fikir). Pdf, sampleLi ile tutarlı olsun diye
    // üçgenin değil mesh'in TOPLAM alanıyla (1/A) hesaplanır.
    if (!m_mesh || m_area <= 0.0f) return 0.0f;
    size_t nTris = m_mesh->numTriangles();
    for (size_t i = 0; i < nTris; ++i) {
        Vec3f p0, p1, p2;
        m_mesh->triangleVertices(i, p0, p1, p2);
        Vec3f e0 = p1 - p0;
        Vec3f e1 = p2 - p0;
        Vec3f n = e0.cross(e1);
        float area2 = n.length();
        if (area2 <= 1e-12f) continue;
        Vec3f nh = n / area2;
        if (std::abs((pLight - p0).dot(nh)) > 1e-3f * (1.0f + area2)) continue;
        float d00 = e0.dot(e0), d01 = e0.dot(e1), d11 = e1.dot(e1);
        Vec3f e2 = pLight - p0;
        float d20 = e2.dot(e0), d21 = e2.dot(e1);
        float denom = d00 * d11 - d01 * d01;
        if (std::abs(denom) <= 1e-12f) continue;
        float v = (d11 * d20 - d01 * d21) / denom;
        float w = (d00 * d21 - d01 * d20) / denom;
        float u = 1.0f - v - w;
        const float eps = 1e-3f;
        if (u < -eps || v < -eps || w < -eps) continue;
        Vec3f toLight = pLight - ref;
        float distSq = toLight.lengthSquared();
        if (distSq <= 0.0f) return 0.0f;
        Vec3f wi = toLight / std::sqrt(distSq);
        return areaSolidAnglePdf(distSq, std::abs(wi.dot(nh)), m_area);
    }
    return 0.0f;
}

Color3f MeshLight::power() const {
    return m_radiance * m_area * PI;
}

} // namespace photon
