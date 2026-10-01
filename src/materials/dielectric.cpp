#include "materials/dielectric.h"
#include "core/math/frame.h"
#include "core/math/utils.h"
#include "core/math/constants.h"
#include <algorithm>
#include <cmath>

namespace photon {

namespace {

bool sharp(float roughness) { return roughness <= 1e-3f; }

float ggxD(float cosThetaH, float alpha) {
    float alpha2 = alpha * alpha;
    float denom = cosThetaH * cosThetaH * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / (PI * denom * denom);
}

float smithG1(float cosTheta, float alpha2) {
    cosTheta = std::abs(cosTheta);
    if (cosTheta <= 1e-6f) return 0.0f;
    float cosTheta2 = cosTheta * cosTheta;
    float tanTheta2 = std::max(0.0f, 1.0f - cosTheta2) / cosTheta2;
    return 2.0f / (1.0f + std::sqrt(1.0f + alpha2 * tanTheta2));
}

Vec3f sampleGGX(const Vec2f& u, float alpha) {
    float x = std::clamp(u.x, 0.0f, 0.999f);
    float theta = std::atan(alpha * std::sqrt(x) / std::sqrt(1.0f - x));
    float phi = TWO_PI * u.y;
    return Vec3f(std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta));
}

struct Local {
    Frame frame;
    Vec3f wo;
    bool entering = true;
};

Local makeLocal(const Vec3f& woWorld, const SurfaceInteraction& si) {
    Vec3f ng = si.ng.lengthSquared() > 1e-12f ? si.ng.normalized() : si.normal.normalized();
    Local g;
    g.frame = Frame(ng);
    g.wo = g.frame.toLocal(woWorld);
    g.entering = g.wo.z > 0.0f;
    return g;
}

float alphaOf(float roughness) {
    float r = std::max(roughness, 0.001f);
    return r * r;
}

Color3f evalRough(const Vec3f& woWorld, const Vec3f& wiWorld, const SurfaceInteraction& si,
                  float ior, float roughness, const Color3f& tint) {
    Local g = makeLocal(woWorld, si);
    if (std::abs(g.wo.z) <= 1e-6f) return Color3f::black();
    Vec3f wi = g.frame.toLocal(wiWorld);
    float alpha = alphaOf(roughness);
    float alpha2 = alpha * alpha;
    float eta = g.entering ? (1.0f / ior) : ior;
    bool reflect = g.wo.z * wi.z > 0.0f;

    if (reflect) {
        Vec3f h = (g.wo + wi).normalized();
        if (h.z < 0.0f) h = -h;
        float woDotH = g.wo.dot(h);
        if (woDotH <= 0.0f) return Color3f::black();
        float D = ggxD(std::abs(h.z), alpha);
        float G = smithG1(g.wo.z, alpha2) * smithG1(wi.z, alpha2);
        float F = fresnelDielectric(woDotH, 1.0f, ior);
        float denom = 4.0f * std::abs(g.wo.z) * std::abs(wi.z);
        if (denom <= 1e-8f) return Color3f::black();
        return tint * (F * D * G / denom);
    }

    Vec3f h = (g.wo + wi * eta).normalized();
    if (h.dot(g.wo) < 0.0f) h = -h;
    float woDotH = g.wo.dot(h);
    float wiDotH = wi.dot(h);
    if (woDotH * wiDotH >= 0.0f) return Color3f::black();
    float D = ggxD(std::abs(h.z), alpha);
    float G = smithG1(g.wo.z, alpha2) * smithG1(wi.z, alpha2);
    float F = fresnelDielectric(woDotH, 1.0f, ior);
    float denomH = woDotH + eta * wiDotH;
    if (std::abs(denomH) <= 1e-6f) return Color3f::black();
    float factor = std::abs(woDotH) * std::abs(wiDotH) /
                   (std::abs(g.wo.z) * std::abs(wi.z) * denomH * denomH);
    return tint * ((1.0f - F) * D * G * factor / (eta * eta));
}

float pdfRough(const Vec3f& woWorld, const Vec3f& wiWorld, const SurfaceInteraction& si,
               float ior, float roughness) {
    Local g = makeLocal(woWorld, si);
    if (std::abs(g.wo.z) <= 1e-6f) return 0.0f;
    Vec3f wi = g.frame.toLocal(wiWorld);
    float alpha = alphaOf(roughness);
    float eta = g.entering ? (1.0f / ior) : ior;
    float fr = fresnelDielectric(g.wo.z, 1.0f, ior);
    bool reflect = g.wo.z * wi.z > 0.0f;

    Vec3f h = reflect ? (g.wo + wi).normalized() : (g.wo + wi * eta).normalized();
    if (h.dot(g.wo) < 0.0f) h = -h;
    float woDotH = g.wo.dot(h);
    if (woDotH <= 0.0f) return 0.0f;
    float D = ggxD(std::abs(h.z), alpha);
    float pdfH = D * std::abs(h.z);
    if (reflect) {
        return fr * pdfH / (4.0f * woDotH);
    }
    float wiDotH = wi.dot(h);
    float denomH = woDotH + eta * wiDotH;
    if (std::abs(denomH) <= 1e-6f) return 0.0f;
    return (1.0f - fr) * pdfH * std::abs(wiDotH) / (denomH * denomH);
}

} // namespace

bool Dielectric::sample(const Vec3f& wo, const SurfaceInteraction& si, const Vec2f& sample,
                        Vec3f& wi, Color3f& brdf, float& pdf) const {
    // ng stays outward. The shading normal is flipped toward the ray, so it cannot
    // tell enter from exit. fresnelDielectric swaps eta when cos < 0 — pass air→ior once.
    Vec3f ng = si.ng.lengthSquared() > 1e-12f ? si.ng.normalized() : si.normal.normalized();
    Frame frame(ng);
    Vec3f woLocal = frame.toLocal(wo);

    bool entering = woLocal.z > 0.0f;
    float eta = entering ? (1.0f / m_ior) : m_ior;
    float fr = fresnelDielectric(woLocal.z, 1.0f, m_ior);

    if (sharp(m_roughness)) {
        if (sample.x < fr) {
            Vec3f wiLocal(-woLocal.x, -woLocal.y, woLocal.z);
            wi = frame.toWorld(wiLocal).normalized();
            pdf = fr;
            brdf = m_tint * fr / std::abs(wiLocal.z);
        } else {
            Vec3f nLocal(0.0f, 0.0f, entering ? 1.0f : -1.0f);
            Vec3f wiLocal;
            if (!refractVec(-woLocal, nLocal, eta, wiLocal)) {
                Vec3f reflLocal(-woLocal.x, -woLocal.y, woLocal.z);
                wi = frame.toWorld(reflLocal).normalized();
                pdf = 1.0f;
                brdf = m_tint / std::abs(reflLocal.z);
                return true;
            }
            wi = frame.toWorld(wiLocal).normalized();
            pdf = 1.0f - fr;
            brdf = m_tint * (1.0f - fr) / (std::abs(wiLocal.z) * eta * eta);
        }
        return true;
    }

    // ponytail: lobe pick uses the macro normal Fresnel, then a GGX half-vector.
    // Upgrade = VNDF + Fresnel on the microfacet normal (one extra sample).
    bool doReflect = sample.x < fr;
    float u0 = doReflect ? sample.x / std::max(fr, 1e-6f)
                         : (sample.x - fr) / std::max(1.0f - fr, 1e-6f);
    Vec3f h = sampleGGX(Vec2f(std::clamp(u0, 0.0f, 0.999f), sample.y), alphaOf(m_roughness));
    if (h.dot(woLocal) < 0.0f) h = -h;

    Vec3f wiLocal;
    if (doReflect) {
        wiLocal = reflectVec(woLocal, h);
        if (wiLocal.z * woLocal.z <= 0.0f) return false;
    } else if (!refractVec(-woLocal, h, eta, wiLocal) || wiLocal.z * woLocal.z >= 0.0f) {
        wiLocal = reflectVec(woLocal, h);
        if (wiLocal.z * woLocal.z <= 0.0f) return false;
    }

    wi = frame.toWorld(wiLocal).normalized();
    brdf = evalRough(wo, wi, si, m_ior, m_roughness, m_tint);
    pdf = pdfRough(wo, wi, si, m_ior, m_roughness);
    return pdf > 0.0f && !brdf.isBlack();
}

Color3f Dielectric::eval(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const {
    if (sharp(m_roughness)) return Color3f::black();
    return evalRough(wo, wi, si, m_ior, m_roughness, m_tint);
}

float Dielectric::pdf(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const {
    if (sharp(m_roughness)) return 0.0f;
    return pdfRough(wo, wi, si, m_ior, m_roughness);
}

} // namespace photon
