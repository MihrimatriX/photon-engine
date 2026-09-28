#include "materials/disney.h"
#include "core/image/image_io.h"
#include "core/math/frame.h"
#include "core/math/utils.h"
#include "core/sampling/sampling.h"
#include <cmath>

namespace photon {

namespace {

float ggxD(float cosThetaH, float alpha) {
    float alpha2 = alpha * alpha;
    float denom = cosThetaH * cosThetaH * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / (PI * denom * denom);
}

float smithG1(float cosTheta, float alpha2) {
    float cosTheta2 = cosTheta * cosTheta;
    float tanTheta2 = std::max(0.0f, 1.0f - cosTheta2) / cosTheta2;
    return 2.0f / (1.0f + std::sqrt(1.0f + alpha2 * tanTheta2));
}

Vec3f sampleGGX(const Vec2f& u, float alpha) {
    float theta = std::atan(alpha * std::sqrt(u.x) / std::sqrt(1.0f - u.x));
    float phi = TWO_PI * u.y;
    return Vec3f(
        std::sin(theta) * std::cos(phi),
        std::sin(theta) * std::sin(phi),
        std::cos(theta)
    );
}

Color3f evalLobes(const Vec3f& wo, const Vec3f& wi, const DisneyMaterial::ShadingParams& p) {
    Frame frame(p.normal);
    Vec3f woLocal = frame.toLocal(wo);
    Vec3f wiLocal = frame.toLocal(wi);

    if (woLocal.z <= 0.0f || wiLocal.z <= 0.0f) {
        return Color3f::black();
    }

    Vec3f hLocal = (woLocal + wiLocal).normalized();

    float cosThetaO = woLocal.z;
    float cosThetaI = wiLocal.z;
    float cosThetaH = hLocal.z;
    float cosThetaD = woLocal.dot(hLocal);

    float F90 = 0.5f + 2.0f * p.roughness * cosThetaD * cosThetaD;
    float Fo = 1.0f + (F90 - 1.0f) * std::pow(1.0f - cosThetaO, 5.0f);
    float Fi = 1.0f + (F90 - 1.0f) * std::pow(1.0f - cosThetaI, 5.0f);
    Color3f fDiffuse = p.baseColor * INV_PI * Fo * Fi * (1.0f - p.metallic);

    float alpha = p.roughness * p.roughness;
    float alpha2 = alpha * alpha;
    float D = ggxD(cosThetaH, alpha);
    float G = smithG1(cosThetaO, alpha2) * smithG1(cosThetaI, alpha2);
    Color3f F0 = lerp(Color3f(0.04f * p.specular), p.baseColor, p.metallic);
    Color3f F = F0 + (Color3f(1.0f) - F0) * std::pow(1.0f - cosThetaD, 5.0f);
    Color3f fSpecular = (F * D * G) / (4.0f * cosThetaO * cosThetaI);

    Color3f fCoat(0.0f);
    if (p.clearCoat > 0.0f) {
        float alphaC = p.clearCoatRoughness * p.clearCoatRoughness;
        float alphaC2 = alphaC * alphaC;
        float Dc = ggxD(cosThetaH, alphaC);
        float Gc = smithG1(cosThetaO, alphaC2) * smithG1(cosThetaI, alphaC2);
        float Fc = 0.04f + 0.96f * std::pow(1.0f - cosThetaD, 5.0f);
        fCoat = Color3f(p.clearCoat * Fc * Dc * Gc / (4.0f * cosThetaO * cosThetaI));
    }

    return fDiffuse + fSpecular + fCoat;
}

float pdfLobes(const Vec3f& wo, const Vec3f& wi, const DisneyMaterial::ShadingParams& p) {
    Frame frame(p.normal);
    Vec3f woLocal = frame.toLocal(wo);
    Vec3f wiLocal = frame.toLocal(wi);

    if (woLocal.z <= 0.0f || wiLocal.z <= 0.0f) {
        return 0.0f;
    }

    Vec3f hLocal = (woLocal + wiLocal).normalized();
    float cosThetaH = hLocal.z;
    float cosThetaD = woLocal.dot(hLocal);
    if (cosThetaD <= 0.0f) return 0.0f;

    float pCoat = 0.25f * p.clearCoat;
    float pSpec = (1.0f - pCoat) * lerp(0.5f, 1.0f, p.metallic);
    float pDiff = 1.0f - pCoat - pSpec;

    float pdfDiffuse = cosineHemispherePdf(wiLocal.z);

    float alpha = p.roughness * p.roughness;
    float D = ggxD(cosThetaH, alpha);
    float pdfSpecular = (D * cosThetaH) / (4.0f * cosThetaD);

    float pdfCoat = 0.0f;
    if (pCoat > 0.0f) {
        float alphaC = p.clearCoatRoughness * p.clearCoatRoughness;
        float Dc = ggxD(cosThetaH, alphaC);
        pdfCoat = (Dc * cosThetaH) / (4.0f * cosThetaD);
    }

    return pDiff * pdfDiffuse + pSpec * pdfSpecular + pCoat * pdfCoat;
}

} // namespace

std::shared_ptr<Image> DisneyMaterial::loadMap(const std::string& path) {
    if (path.empty()) return nullptr;
    if (auto ldr = loadImageLDR(path)) {
        return std::make_shared<Image>(std::move(*ldr));
    }
    if (auto hdr = loadImageHDR(path)) {
        return std::make_shared<Image>(std::move(*hdr));
    }
    if (auto exr = loadImageEXR(path)) {
        return std::make_shared<Image>(std::move(*exr));
    }
    return nullptr;
}

void DisneyMaterial::setAlbedoMap(const std::string& p) {
    m_albedoMap = p;
    m_albedoTex = loadMap(p);
}

void DisneyMaterial::setNormalMap(const std::string& p) {
    m_normalMap = p;
    m_normalTex = loadMap(p);
}

void DisneyMaterial::setRoughnessMap(const std::string& p) {
    m_roughnessMap = p;
    m_roughnessTex = loadMap(p);
}

void DisneyMaterial::setMetalnessMap(const std::string& p) {
    m_metalnessMap = p;
    m_metalnessTex = loadMap(p);
}

DisneyMaterial::ShadingParams DisneyMaterial::resolve(const SurfaceInteraction& si) const {
    ShadingParams p;
    p.baseColor = m_baseColor;
    p.metallic = m_metallic;
    p.roughness = m_roughness;
    p.specular = m_specular;
    p.clearCoat = m_clearCoat;
    p.clearCoatRoughness = m_clearCoatRoughness;
    p.normal = si.normal;

    if (m_albedoTex) {
        p.baseColor = m_albedoTex->sampleBilinear(si.uv.x, si.uv.y);
    }
    if (m_roughnessTex) {
        Color3f c = m_roughnessTex->sampleBilinear(si.uv.x, si.uv.y);
        // ponytail: use green channel (glTF ORM) with R fallback
        p.roughness = std::max(0.001f, c.g > 1e-6f ? c.g : c.r);
    }
    if (m_metalnessTex) {
        Color3f c = m_metalnessTex->sampleBilinear(si.uv.x, si.uv.y);
        p.metallic = std::clamp(c.b > 1e-6f ? c.b : c.r, 0.0f, 1.0f);
    }
    if (m_normalTex) {
        Color3f c = m_normalTex->sampleBilinear(si.uv.x, si.uv.y);
        Vec3f nTs(c.r * 2.0f - 1.0f, c.g * 2.0f - 1.0f, c.b * 2.0f - 1.0f);
        Vec3f T = si.tangent;
        if (T.lengthSquared() < 1e-8f) {
            Frame f(si.normal);
            T = f.s;
        }
        Vec3f B = si.normal.cross(T);
        if (B.lengthSquared() < 1e-8f) {
            Frame f(si.normal);
            B = f.t;
            T = f.s;
        } else {
            B = B.normalized();
            T = B.cross(si.normal).normalized();
        }
        p.normal = (T * nTs.x + B * nTs.y + si.normal * nTs.z).normalized();
        if (p.normal.dot(si.normal) < 0.0f) p.normal = -p.normal;
    }
    return p;
}

bool DisneyMaterial::sample(const Vec3f& wo, const SurfaceInteraction& si, const Vec2f& sample,
                            Vec3f& wi, Color3f& brdf, float& pdf) const {
    ShadingParams p = resolve(si);
    Frame frame(p.normal);
    Vec3f woLocal = frame.toLocal(wo);

    if (woLocal.z <= 0.0f) {
        return false;
    }

    float pCoat = 0.25f * p.clearCoat;
    float pSpec = (1.0f - pCoat) * lerp(0.5f, 1.0f, p.metallic);
    float pDiff = 1.0f - pCoat - pSpec;

    Vec3f wiLocal;
    float xi = sample.x;

    if (pCoat > 0.0f && xi < pCoat) {
        float scaledX = xi / pCoat;
        float alpha = p.clearCoatRoughness * p.clearCoatRoughness;
        Vec3f hLocal = sampleGGX(Vec2f(scaledX, sample.y), alpha);
        wiLocal = reflectVec(woLocal, hLocal);
        if (wiLocal.z <= 0.0f) return false;
    } else if (xi < pCoat + pSpec) {
        float scaledX = (xi - pCoat) / std::max(pSpec, 1e-6f);
        float alpha = p.roughness * p.roughness;
        Vec3f hLocal = sampleGGX(Vec2f(scaledX, sample.y), alpha);
        wiLocal = reflectVec(woLocal, hLocal);
        if (wiLocal.z <= 0.0f) return false;
    } else {
        float scaledX = (xi - pCoat - pSpec) / std::max(pDiff, 1e-6f);
        wiLocal = cosineSampleHemisphere(Vec2f(scaledX, sample.y));
    }

    wi = frame.toWorld(wiLocal).normalized();
    brdf = evalLobes(wo, wi, p);
    pdf = pdfLobes(wo, wi, p);
    return pdf > 0.0f;
}

Color3f DisneyMaterial::eval(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const {
    return evalLobes(wo, wi, resolve(si));
}

float DisneyMaterial::pdf(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const {
    return pdfLobes(wo, wi, resolve(si));
}

} // namespace photon
