// area_light.cpp — Dikdörtgen ve mesh alan ışıklarının örneklenmesi ve pdf'i.
#include "lights/area_light.h"
#include "geometry/mesh.h"
#include "core/math/constants.h"
#include "core/math/frame.h"
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

/// Bu katı açının altında küresel dikdörtgen formülleri float'ta kararsızlaşır (açıların
/// toplamı − 2π iptali); o zaman alan örneklemesi zaten iyidir (ışık küçük ve uzak).
constexpr float kMinSolidAngle = 1e-3f;

float safeAsin(float x) { return std::asin(std::clamp(x, -1.0f, 1.0f)); }

/// İki birim vektör arasındaki açı, küçük açılarda da hassas (PBRT-v4 AngleBetween).
float angleBetween(const Vec3f& a, const Vec3f& b) {
    if (a.dot(b) < 0.0f) return PI - 2.0f * safeAsin((a + b).length() * 0.5f);
    return 2.0f * safeAsin((b - a).length() * 0.5f);
}

/// Ureña, Fajardo, King 2013 "An Area-Preserving Parametrization for Spherical
/// Rectangles". Dikdörtgen, alıcıyı orijin alan yerel çerçevede z = z0 < 0 düzlemindedir;
/// köşelerden geçen dört büyük çember düzleminin normalleri n0..n3, iç açıları g0..g3.
/// Küresel dörtgenin katı açısı Ω = g0 + g1 + g2 + g3 − 2π (Girard teoremi).
struct SphericalRect {
    Frame frame;
    float x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0;
    float b0 = 0, b1 = 0, g2g3 = 0;
    float solidAngle = 0.0f;
};

SphericalRect setupSphericalRect(const Vec3f& ref, const Vec3f& corner, const Vec3f& ex, const Vec3f& ey) {
    SphericalRect r;
    const float exl = ex.length(), eyl = ey.length();
    const Vec3f x = ex / exl, y = ey / eyl;
    Vec3f z = x.cross(y);
    const Vec3f d = corner - ref;
    r.z0 = d.dot(z);
    if (r.z0 > 0.0f) { // z dikdörtgenden uzağa baksın
        z = -z;
        r.z0 = -r.z0;
    }
    r.frame = Frame(x, y, z);
    r.x0 = d.dot(x);
    r.y0 = d.dot(y);
    r.x1 = r.x0 + exl;
    r.y1 = r.y0 + eyl;
    if (r.z0 > -1e-7f) return r; // alıcı ışığın düzleminde
    const Vec3f v00(r.x0, r.y0, r.z0), v01(r.x0, r.y1, r.z0), v10(r.x1, r.y0, r.z0), v11(r.x1, r.y1, r.z0);
    const Vec3f n0 = v00.cross(v10).normalized();
    const Vec3f n1 = v10.cross(v11).normalized();
    const Vec3f n2 = v11.cross(v01).normalized();
    const Vec3f n3 = v01.cross(v00).normalized();
    const float g0 = angleBetween(-n0, n1), g1 = angleBetween(-n1, n2);
    const float g2 = angleBetween(-n2, n3), g3 = angleBetween(-n3, n0);
    r.solidAngle = g0 + g1 + g2 + g3 - TWO_PI;
    r.b0 = n0.z;
    r.b1 = n2.z;
    r.g2g3 = g2 + g3;
    return r;
}

/// Katı açıda düzgün nokta: önce u0 ile x kesiti (alan korunumlu), sonra u1 ile y.
Vec3f sampleSphericalRect(const SphericalRect& r, const Vec2f& u) {
    // φu = u·Ω + k, k = 2π − g2 − g3 (makale); 2π periyodu atılınca u·Ω − (g2 + g3).
    const float au = u.x * r.solidAngle - r.g2g3;
    const float fu = (std::cos(au) * r.b0 - r.b1) / std::sin(au);
    float cu = std::copysign(1.0f, fu) / std::sqrt(fu * fu + r.b0 * r.b0);
    cu = std::clamp(cu, -0.9999999f, 0.9999999f);
    float xu = -(cu * r.z0) / std::sqrt(std::max(0.0f, 1.0f - cu * cu));
    xu = std::clamp(xu, r.x0, r.x1);
    const float dd = std::sqrt(xu * xu + r.z0 * r.z0);
    const float h0 = r.y0 / std::sqrt(dd * dd + r.y0 * r.y0);
    const float h1 = r.y1 / std::sqrt(dd * dd + r.y1 * r.y1);
    const float hv = h0 + u.y * (h1 - h0);
    const float hv2 = hv * hv;
    const float yv = hv2 < 1.0f - 1e-6f ? hv * dd / std::sqrt(1.0f - hv2) : r.y1;
    return r.frame.toWorld(Vec3f(xu, std::clamp(yv, r.y0, r.y1), r.z0));
}

} // namespace

float AreaLight::solidAngle(const Vec3f& ref) const {
    if (!m_rectangle) return 0.0f;
    return std::max(0.0f, setupSphericalRect(ref, m_position, m_u, m_v).solidAngle);
}

float AreaLight::importance(const Vec3f& p, const Vec3f& n) const {
    const Vec3f c = m_position + (m_u + m_v) * 0.5f;
    const Vec3f d = c - p;
    const float dist2 = d.lengthSquared();
    const float lum = std::max(0.0f, m_radiance.luminance());
    if (!(dist2 > 0.0f)) return lum * TWO_PI;
    const Vec3f w = d / std::sqrt(dist2);
    // Görülen katı açı ~ A·cosθl/r² (büyük, yakın ışıkta 2π ile sınırlı). Kenardan bakışta
    // bile büyük bir softbox'ın bir kısmı görünür: cosθl'ye taban.
    const float cosL = std::max(std::abs(w.dot(m_normal)), 0.2f);
    const float omega = std::min(m_area * cosL / dist2, TWO_PI);
    return lum * omega * receiverCosine(n, w);
}

LightSample AreaLight::sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const {
    LightSample ls;

    if (m_rectangle) {
        const SphericalRect r = setupSphericalRect(si.point, m_position, m_u, m_v);
        if (r.solidAngle >= kMinSolidAngle) {
            const Vec3f toLight = sampleSphericalRect(r, sample);
            const float dist = toLight.length();
            if (!(dist > 0.0f)) return ls;
            ls.distance = dist;
            ls.wi = toLight / dist;
            ls.Li = m_radiance;
            ls.pdf = 1.0f / r.solidAngle; // katı açıda düzgün
            return ls;
        }
    }

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
    if (!(std::abs(det) > 1e-12f * uu * vv)) return 0.0f; // göreli: mm ve km ölçekli ışıklar

    float s = (vv * du - uv * dv) / det;
    float t = (uu * dv - uv * du) / det;
    const float eps = 1e-3f;
    if (s < -eps || t < -eps || s > 1.0f + eps || t > 1.0f + eps) return 0.0f;

    if (m_rectangle) {
        const float omega = setupSphericalRect(ref, m_position, m_u, m_v).solidAngle;
        if (omega >= kMinSolidAngle) return 1.0f / omega;
    }

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
    Vec3f centroid(0.0f);
    for (size_t i = 0; i < n; ++i) {
        const float a = m_mesh->triangleArea(i);
        sum += a;
        m_cdf[i] = sum;
        Vec3f p0, p1, p2;
        m_mesh->triangleVertices(i, p0, p1, p2);
        centroid += (p0 + p1 + p2) * (a / 3.0f);
    }
    m_area = sum;
    m_center = sum > 0.0f ? centroid / sum : centroid;
}

float MeshLight::importance(const Vec3f& p, const Vec3f& n) const {
    const Vec3f d = m_center - p;
    const float dist2 = d.lengthSquared();
    const float lum = std::max(0.0f, m_radiance.luminance());
    if (!(dist2 > 0.0f)) return lum * TWO_PI;
    // Eğri yayıcı (ampul, şerit): ortalama |cosθl| ≈ 0.5.
    const float omega = std::min(m_area * 0.5f / dist2, TWO_PI);
    return lum * omega * receiverCosine(n, d / std::sqrt(dist2));
}

float MeshLight::pdfLiHit(const Vec3f& ref, const SurfaceInteraction& hit) const {
    if (m_area <= 0.0f) return 0.0f;
    const Vec3f toLight = hit.point - ref;
    const float distSq = toLight.lengthSquared();
    if (distSq <= 0.0f) return 0.0f;
    const Vec3f ng = hit.ng.lengthSquared() > 0.0f ? hit.ng.normalized() : hit.normal;
    return areaSolidAnglePdf(distSq, std::abs((toLight / std::sqrt(distSq)).dot(ng)), m_area);
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
