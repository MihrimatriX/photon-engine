// shape_lights.cpp — Disk, küre ve silindir ışıklarının örneklemesi, pdf'leri ve sınırları.
#include "lights/shape_lights.h"
#include "core/math/constants.h"
#include "core/math/frame.h"
#include "core/sampling/sampling.h"
#include <algorithm>
#include <cmath>

namespace photon {

namespace {

float safeSqrtf(float x) { return std::sqrt(std::max(0.0f, x)); }

/// Alan pdf'i → katı açı pdf'i: p_ω = r² / (|cos θl| · A).
float areaToSolidAngle(float distSq, float cosAbs, float area) {
    if (!(cosAbs > 1e-7f) || !(area > 0.0f) || !(distSq > 0.0f)) return 0.0f;
    return distSq / (area * cosAbs);
}

/// sin²θmax bu değerin altındaysa (θmax < 1.5°) 1 − cos θmax float'ta iptal hatasına düşer;
/// Taylor açılımı kullanılır (PBRT-v4 Sphere::Sample).
constexpr float kSmallSin2 = 0.00068523f;

/// Küre ışığının koni pdf'inin paydası 1 − cos θmax; ref içerideyse 0.
float sphereOneMinusCosMax(const Vec3f& ref, const Vec3f& center, float radius, float& sin2ThetaMax) {
    const float dc2 = (center - ref).lengthSquared();
    const float r2 = radius * radius;
    if (!(dc2 > r2 * (1.0f + 1e-5f))) return 0.0f;
    sin2ThetaMax = r2 / dc2;
    if (sin2ThetaMax < kSmallSin2) return 0.5f * sin2ThetaMax;
    return 1.0f - safeSqrtf(1.0f - sin2ThetaMax);
}

} // namespace

// ── Disk ──────────────────────────────────────────────────────────────────

DiskLight::DiskLight(const Vec3f& center, const Vec3f& ex, const Vec3f& ey, const Color3f& radiance, bool twoSided)
    : m_center(center), m_ex(ex), m_ey(ey), m_radiance(radiance), m_twoSided(twoSided) {
    const Vec3f n = ex.cross(ey);
    const float len = n.length();
    // Birim diskin doğrusal görüntüsü: alan π |ex × ey| (eksenler dik olmasa da).
    m_area = PI * len;
    m_normal = len > 0.0f ? n / len : Vec3f(0, 0, 1);
}

LightSample DiskLight::sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const {
    LightSample ls;
    if (!(m_area > 0.0f) || m_radiance.isBlack() || !facing(si.point)) return ls;
    // Eşmerkezli eşleme birim diskte alan-düzgündür; doğrusal dönüşüm düzgünlüğü korur.
    const Vec2f d = uniformSampleDisk(sample);
    const Vec3f pLight = m_center + m_ex * d.x + m_ey * d.y;
    const Vec3f toLight = pLight - si.point;
    const float distSq = toLight.lengthSquared();
    if (!(distSq > 0.0f)) return ls;
    ls.distance = std::sqrt(distSq);
    ls.wi = toLight / ls.distance;
    ls.pdf = areaToSolidAngle(distSq, std::abs(ls.wi.dot(m_normal)), m_area);
    if (ls.pdf > 0.0f) ls.Li = m_radiance;
    return ls;
}

float DiskLight::pdfLiHit(const Vec3f& ref, const SurfaceInteraction& hit) const {
    if (!facing(ref)) return 0.0f;
    const Vec3f toLight = hit.point - ref;
    const float distSq = toLight.lengthSquared();
    if (!(distSq > 0.0f)) return 0.0f;
    return areaToSolidAngle(distSq, std::abs(toLight.dot(m_normal)) / std::sqrt(distSq), m_area);
}

float DiskLight::importance(const Vec3f& p, const Vec3f& n) const {
    if (!facing(p)) return 0.0f;
    const Vec3f d = m_center - p;
    const float dist2 = d.lengthSquared();
    const float lum = std::max(0.0f, m_radiance.luminance());
    if (!(dist2 > 0.0f)) return lum * TWO_PI;
    const Vec3f w = d / std::sqrt(dist2);
    const float cosL = std::max(std::abs(w.dot(m_normal)), 0.2f);
    const float omega = std::min(m_area * cosL / dist2, TWO_PI);
    return lum * omega * receiverCosine(n, w);
}

bool DiskLight::bounds(LightBounds& out) const {
    if (!(m_area > 0.0f) || m_radiance.isBlack()) return false;
    const Vec3f e(std::sqrt(m_ex.x * m_ex.x + m_ey.x * m_ey.x), std::sqrt(m_ex.y * m_ex.y + m_ey.y * m_ey.y),
                  std::sqrt(m_ex.z * m_ex.z + m_ey.z * m_ey.z));
    out.bounds = AABB(m_center - e, m_center + e);
    out.w = m_normal;
    out.phi = std::max(0.0f, m_radiance.luminance()) * m_area * PI;
    out.cosThetaO = 1.0f;
    out.cosThetaE = 0.0f;
    out.twoSided = m_twoSided;
    return true;
}

Color3f DiskLight::power() const {
    return m_radiance * (m_area * PI * (m_twoSided ? 2.0f : 1.0f));
}

// ── Küre ──────────────────────────────────────────────────────────────────

SphereLight::SphereLight(const Vec3f& center, float radius, const Color3f& radiance)
    : m_center(center), m_radius(std::max(0.0f, radius)), m_radiance(radiance) {}

float SphereLight::solidAngle(const Vec3f& ref) const {
    float sin2 = 0.0f;
    return TWO_PI * sphereOneMinusCosMax(ref, m_center, m_radius, sin2);
}

LightSample SphereLight::sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const {
    LightSample ls;
    if (!(m_radius > 0.0f) || m_radiance.isBlack()) return ls;
    // Küre dışa ışır: içerideki alıcı yalnız iç yüzü (arka yüz) görür, ışık almaz.
    float sin2ThetaMax = 0.0f;
    const float oneMinusCosMax = sphereOneMinusCosMax(si.point, m_center, m_radius, sin2ThetaMax);
    if (!(oneMinusCosMax > 0.0f)) return ls;

    // Koni içinde katı açıda düzgün θ, sonra küre üzerindeki noktaya merkezden α açısı
    // (PBRT-v4 §6.2.4). Küçük açıda sin²θ doğrudan örneklenir.
    const float sinThetaMax = std::sqrt(sin2ThetaMax);
    float cosTheta = 1.0f - oneMinusCosMax * sample.x;
    float sin2Theta = 1.0f - cosTheta * cosTheta;
    if (sin2ThetaMax < kSmallSin2) {
        sin2Theta = sin2ThetaMax * sample.x;
        cosTheta = std::sqrt(1.0f - sin2Theta);
    }
    const float cosAlpha = sin2Theta / sinThetaMax + cosTheta * safeSqrtf(1.0f - sin2Theta / sin2ThetaMax);
    const float sinAlpha = safeSqrtf(1.0f - cosAlpha * cosAlpha);
    const float phi = sample.y * TWO_PI;
    const Frame f((m_center - si.point).normalized());
    // Örnek noktadaki dış normal alıcıya doğru bakar (−z tarafı).
    const Vec3f nLight = f.toWorld(Vec3f(-sinAlpha * std::cos(phi), -sinAlpha * std::sin(phi), -cosAlpha));
    const Vec3f pLight = m_center + nLight * m_radius;
    const Vec3f toLight = pLight - si.point;
    const float distSq = toLight.lengthSquared();
    if (!(distSq > 0.0f)) return ls;
    ls.distance = std::sqrt(distSq);
    ls.wi = toLight / ls.distance;
    ls.Li = m_radiance;
    ls.pdf = 1.0f / (TWO_PI * oneMinusCosMax);
    return ls;
}

float SphereLight::pdfLiHit(const Vec3f& ref, const SurfaceInteraction& /*hit*/) const {
    float sin2 = 0.0f;
    const float oneMinusCosMax = sphereOneMinusCosMax(ref, m_center, m_radius, sin2);
    return oneMinusCosMax > 0.0f ? 1.0f / (TWO_PI * oneMinusCosMax) : 0.0f;
}

float SphereLight::importance(const Vec3f& p, const Vec3f& n) const {
    const float omega = solidAngle(p);
    if (!(omega > 0.0f)) return 0.0f;
    const Vec3f w = (m_center - p).normalized();
    return std::max(0.0f, m_radiance.luminance()) * omega * receiverCosine(n, w);
}

bool SphereLight::bounds(LightBounds& out) const {
    if (!(m_radius > 0.0f) || m_radiance.isBlack()) return false;
    out.bounds = AABB(m_center - Vec3f(m_radius), m_center + Vec3f(m_radius));
    out.w = Vec3f(0, 1, 0);
    // π × şiddet (nokta ve alan ışıklarıyla aynı ölçü): I = L · π r².
    out.phi = PI * std::max(0.0f, m_radiance.luminance()) * PI * m_radius * m_radius;
    out.cosThetaO = -1.0f; // her yöne
    out.cosThetaE = 0.0f;
    out.twoSided = false;
    return true;
}

Color3f SphereLight::power() const {
    // Φ = L · π · A, A = 4π r².
    return m_radiance * (PI * 4.0f * PI * m_radius * m_radius);
}

// ── Silindir ──────────────────────────────────────────────────────────────

CylinderLight::CylinderLight(const Vec3f& center, const Vec3f& halfAxis, float radius, const Color3f& radiance)
    : m_center(center), m_radius(std::max(0.0f, radius)), m_radiance(radiance) {
    m_halfLength = halfAxis.length();
    m_axis = m_halfLength > 0.0f ? halfAxis / m_halfLength : Vec3f(1, 0, 0);
    const Frame f(m_axis);
    m_b = f.s;
    m_c = f.t;
}

float CylinderLight::area() const {
    return TWO_PI * m_radius * 2.0f * m_halfLength;
}

bool CylinderLight::visibleArc(const Vec3f& ref, float& phi0, float& alpha) const {
    // Yan yüzeydeki p noktası (açı φ) ref'e bakıyorsa (ref − p)·n > 0 ⇔ ρ cos(φ − φ0) > r:
    // boydan bağımsız, eksen etrafında φ0 ± arccos(r / ρ) aralığı. ρ ≤ r: hiçbir dış yüz görünmez.
    const Vec3f q = ref - m_center;
    const Vec3f qp = q - m_axis * q.dot(m_axis);
    const float rho = qp.length();
    if (!(rho > m_radius * (1.0f + 1e-5f))) return false;
    phi0 = std::atan2(qp.dot(m_c), qp.dot(m_b));
    alpha = std::acos(std::clamp(m_radius / rho, 0.0f, 1.0f));
    return alpha > 0.0f;
}

LightSample CylinderLight::sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const {
    LightSample ls;
    if (!(m_radius > 0.0f) || !(m_halfLength > 0.0f) || m_radiance.isBlack()) return ls;
    float phi0 = 0.0f, alpha = 0.0f;
    if (!visibleArc(si.point, phi0, alpha)) return ls;
    // Görünür şeritte alan-düzgün: φ ∈ φ0 ± α, z ∈ [−h, h]; alan 2α · r · 2h.
    const float phi = phi0 + alpha * (2.0f * sample.x - 1.0f);
    const float z = m_halfLength * (2.0f * sample.y - 1.0f);
    const Vec3f nLight = m_b * std::cos(phi) + m_c * std::sin(phi);
    const Vec3f pLight = m_center + m_axis * z + nLight * m_radius;
    const Vec3f toLight = pLight - si.point;
    const float distSq = toLight.lengthSquared();
    if (!(distSq > 0.0f)) return ls;
    ls.distance = std::sqrt(distSq);
    ls.wi = toLight / ls.distance;
    const float cosL = -ls.wi.dot(nLight);
    ls.pdf = areaToSolidAngle(distSq, cosL, 2.0f * alpha * m_radius * 2.0f * m_halfLength);
    if (ls.pdf > 0.0f) ls.Li = m_radiance;
    return ls;
}

float CylinderLight::pdfLiHit(const Vec3f& ref, const SurfaceInteraction& hit) const {
    float phi0 = 0.0f, alpha = 0.0f;
    if (!visibleArc(ref, phi0, alpha)) return 0.0f;
    const Vec3f toLight = hit.point - ref;
    const float distSq = toLight.lengthSquared();
    if (!(distSq > 0.0f)) return 0.0f;
    // Silindirin dış normali (ng) alıcıya bakmıyorsa (uçtan içeri giren ışın iç yüze çarptı)
    // bu nokta örneklenmez.
    const float cosL = -toLight.dot(hit.ng) / std::sqrt(distSq);
    return areaToSolidAngle(distSq, cosL, 2.0f * alpha * m_radius * 2.0f * m_halfLength);
}

float CylinderLight::importance(const Vec3f& p, const Vec3f& n) const {
    float phi0 = 0.0f, alpha = 0.0f;
    if (!visibleArc(p, phi0, alpha)) return 0.0f;
    const Vec3f d = m_center - p;
    const float dist2 = d.lengthSquared();
    const float lum = std::max(0.0f, m_radiance.luminance());
    if (!(dist2 > 0.0f)) return lum * TWO_PI;
    const Vec3f w = d / std::sqrt(dist2);
    // Görülen izdüşüm ≈ 2 r ℓ sin(eksen, bakış); uçtan bakışta taban bırakılır.
    const float cosA = w.dot(m_axis);
    const float sinA = std::max(safeSqrtf(1.0f - cosA * cosA), 0.2f);
    const float omega = std::min(2.0f * m_radius * 2.0f * m_halfLength * sinA / dist2, TWO_PI);
    return lum * omega * receiverCosine(n, w);
}

bool CylinderLight::bounds(LightBounds& out) const {
    if (!(m_radius > 0.0f) || !(m_halfLength > 0.0f) || m_radiance.isBlack()) return false;
    const Vec3f a = m_axis.cwiseAbs();
    const Vec3f e(a.x * m_halfLength + m_radius * safeSqrtf(1.0f - a.x * a.x),
                  a.y * m_halfLength + m_radius * safeSqrtf(1.0f - a.y * a.y),
                  a.z * m_halfLength + m_radius * safeSqrtf(1.0f - a.z * a.z));
    out.bounds = AABB(m_center - e, m_center + e);
    out.w = m_axis;
    // π × yandan görülen şiddet I = L · 2 r ℓ.
    out.phi = PI * std::max(0.0f, m_radiance.luminance()) * 2.0f * m_radius * 2.0f * m_halfLength;
    out.cosThetaO = -1.0f;
    out.cosThetaE = 0.0f;
    out.twoSided = false;
    return true;
}

Color3f CylinderLight::power() const {
    return m_radiance * (PI * area());
}

} // namespace photon
