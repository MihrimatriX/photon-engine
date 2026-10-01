#include "lights/area_light.h"
#include "geometry/mesh.h"
#include "core/math/constants.h"
#include "core/sampling/sampling.h"
#include <algorithm>
#include <cmath>

namespace photon {

namespace {

float areaSolidAnglePdf(float distSq, float cosAbs, float area) {
    if (!(cosAbs > 1e-6f) || !(area > 0.0f) || !(distSq > 0.0f)) return 0.0f;
    return distSq / (area * cosAbs);
}

} // namespace

LightSample AreaLight::sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const {
    LightSample ls;
    
    // Sample point on the light source rectangle
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
    return m_radiance * m_area * PI;
}

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

    float pick = std::clamp(sample.x, 0.0f, 0.999f) * m_area;
    auto it = std::lower_bound(m_cdf.begin(), m_cdf.end(), pick);
    size_t tri = static_cast<size_t>(std::min(it, m_cdf.end() - 1) - m_cdf.begin());
    float prev = tri == 0 ? 0.0f : m_cdf[tri - 1];
    float triArea = std::max(m_cdf[tri] - prev, 1e-12f);
    float u0 = (pick - prev) / triArea;

    Vec3f p0, p1, p2;
    m_mesh->triangleVertices(tri, p0, p1, p2);
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
