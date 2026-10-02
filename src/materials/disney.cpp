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
    float x = std::clamp(u.x, 0.0f, 0.999f);
    float theta = std::atan(alpha * std::sqrt(x) / std::sqrt(1.0f - x));
    float phi = TWO_PI * u.y;
    return Vec3f(
        std::sin(theta) * std::cos(phi),
        std::sin(theta) * std::sin(phi),
        std::cos(theta)
    );
}

void anisoAlpha(float roughness, float anisotropy, float& ax, float& ay) {
    float aspect = std::sqrt(std::max(0.0f, 1.0f - 0.9f * std::clamp(anisotropy, 0.0f, 1.0f)));
    float a2 = std::max(0.001f, roughness * roughness);
    ax = std::max(0.001f, a2 / aspect);
    ay = std::max(0.001f, a2 * aspect);
}

float ggxDAniso(const Vec3f& h, float ax, float ay) {
    float cosTheta = h.z;
    if (cosTheta <= 1e-6f) return 0.0f;
    float cos2 = cosTheta * cosTheta;
    float sin2 = std::max(0.0f, 1.0f - cos2);
    float tan2 = sin2 / cos2;
    float sinTheta = std::sqrt(sin2);
    float cosPhi = sinTheta > 0.0f ? h.x / sinTheta : 1.0f;
    float sinPhi = sinTheta > 0.0f ? h.y / sinTheta : 0.0f;
    float e = tan2 * (cosPhi * cosPhi / (ax * ax) + sinPhi * sinPhi / (ay * ay));
    float cos4 = cos2 * cos2;
    return 1.0f / (PI * ax * ay * cos4 * (1.0f + e) * (1.0f + e));
}

float smithG1Aniso(const Vec3f& w, float ax, float ay) {
    float cosTheta = std::abs(w.z);
    if (cosTheta <= 1e-6f) return 0.0f;
    float cos2 = cosTheta * cosTheta;
    float sin2 = std::max(0.0f, 1.0f - cos2);
    if (sin2 <= 0.0f) return 1.0f;
    float tanTheta = std::sqrt(sin2) / cosTheta;
    float sinTheta = std::sqrt(sin2);
    float cosPhi = w.x / sinTheta;
    float sinPhi = w.y / sinTheta;
    float alpha = std::sqrt(cosPhi * cosPhi * ax * ax + sinPhi * sinPhi * ay * ay);
    float a2t2 = (alpha * tanTheta) * (alpha * tanTheta);
    return 2.0f / (1.0f + std::sqrt(1.0f + a2t2));
}

Vec3f sampleGGXAniso(const Vec2f& u, float ax, float ay) {
    float u1 = std::clamp(u.x, 0.0f, 0.999f);
    float phi = std::atan((ay / ax) * std::tan(TWO_PI * u.y + 0.5f * PI));
    if (u.y > 0.5f) phi += PI;
    float sinPhi = std::sin(phi);
    float cosPhi = std::cos(phi);
    float ax2 = ax * ax;
    float ay2 = ay * ay;
    float alpha2 = 1.0f / (cosPhi * cosPhi / ax2 + sinPhi * sinPhi / ay2);
    float tanTheta2 = alpha2 * u1 / std::max(1e-6f, 1.0f - u1);
    float cosTheta = 1.0f / std::sqrt(1.0f + tanTheta2);
    float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    return Vec3f(sinTheta * cosPhi, sinTheta * sinPhi, cosTheta);
}

Frame shadingFrame(const Vec3f& nIn, const Vec3f& tangentIn) {
    Vec3f n = nIn.lengthSquared() > 0.0f ? nIn.normalized() : Vec3f(0, 1, 0);
    Vec3f t = tangentIn - n * tangentIn.dot(n);
    if (t.lengthSquared() < 1e-8f) return Frame(n);
    t = t.normalized();
    Vec3f b = n.cross(t);
    if (b.lengthSquared() < 1e-8f) return Frame(n);
    b = b.normalized();
    t = b.cross(n).normalized();
    return Frame(t, b, n);
}

float lobeD(const Vec3f& h, float roughness, float anisotropy) {
    if (anisotropy <= 1e-4f) return ggxD(std::max(0.0f, h.z), roughness * roughness);
    float ax, ay;
    anisoAlpha(roughness, anisotropy, ax, ay);
    return ggxDAniso(h, ax, ay);
}

float lobeG(const Vec3f& wo, const Vec3f& wi, float roughness, float anisotropy) {
    if (anisotropy <= 1e-4f) {
        float alpha = roughness * roughness;
        float alpha2 = alpha * alpha;
        return smithG1(wo.z, alpha2) * smithG1(wi.z, alpha2);
    }
    float ax, ay;
    anisoAlpha(roughness, anisotropy, ax, ay);
    return smithG1Aniso(wo, ax, ay) * smithG1Aniso(wi, ax, ay);
}

Color3f evalLobes(const Vec3f& wo, const Vec3f& wi, const DisneyMaterial::ShadingParams& p) {
    Frame frame = shadingFrame(p.normal, p.tangent);
    Vec3f woLocal = frame.toLocal(wo);
    Vec3f wiLocal = frame.toLocal(wi);

    if (woLocal.z <= 0.0f) return Color3f::black();

    float trans = std::clamp(p.diffuseTransmission, 0.0f, 1.0f) * (1.0f - p.metallic);
    if (wiLocal.z <= 0.0f) {
        if (trans <= 0.0f) return Color3f::black();
        return p.baseColor * trans * INV_PI;
    }

    Vec3f hLocal = (woLocal + wiLocal).normalized();

    float cosThetaO = woLocal.z;
    float cosThetaI = wiLocal.z;
    float cosThetaH = hLocal.z;
    float cosThetaD = woLocal.dot(hLocal);

    float F90 = 0.5f + 2.0f * p.roughness * cosThetaD * cosThetaD;
    float Fo = 1.0f + (F90 - 1.0f) * std::pow(1.0f - cosThetaO, 5.0f);
    float Fi = 1.0f + (F90 - 1.0f) * std::pow(1.0f - cosThetaI, 5.0f);
    Color3f fDiffuse = p.baseColor * INV_PI * Fo * Fi * (1.0f - p.metallic) * (1.0f - trans);

    float D = lobeD(hLocal, p.roughness, p.anisotropy);
    float G = lobeG(woLocal, wiLocal, p.roughness, p.anisotropy);
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

    Color3f fSheen(0.0f);
    if (p.sheen > 0.0f) {
        // ponytail: sheen rides the diffuse sample; a dedicated sheen lobe if cloth noise matters
        float fh = std::pow(std::max(0.0f, 1.0f - cosThetaD), 5.0f);
        float lum = p.baseColor.luminance();
        Color3f tint = lum > 1e-6f ? p.baseColor * (1.0f / lum) : Color3f(1.0f);
        Color3f csheen = Color3f(1.0f) * 0.5f + tint * 0.5f;
        fSheen = csheen * (p.sheen * fh * (1.0f - p.metallic));
    }

    return fDiffuse + fSpecular + fCoat + fSheen;
}

void lobeWeights(const DisneyMaterial::ShadingParams& p, float& pCoat, float& pSpec,
                 float& pDiffRefl, float& pTrans) {
    pCoat = 0.25f * p.clearCoat;
    pSpec = (1.0f - pCoat) * lerp(0.5f, 1.0f, p.metallic);
    float pDiff = 1.0f - pCoat - pSpec;
    float trans = std::clamp(p.diffuseTransmission, 0.0f, 1.0f) * (1.0f - p.metallic);
    pTrans = pDiff * trans;
    pDiffRefl = pDiff - pTrans;
}

float pdfLobes(const Vec3f& wo, const Vec3f& wi, const DisneyMaterial::ShadingParams& p) {
    Frame frame = shadingFrame(p.normal, p.tangent);
    Vec3f woLocal = frame.toLocal(wo);
    Vec3f wiLocal = frame.toLocal(wi);

    if (woLocal.z <= 0.0f) return 0.0f;

    float pCoat, pSpec, pDiffRefl, pTrans;
    lobeWeights(p, pCoat, pSpec, pDiffRefl, pTrans);

    if (wiLocal.z <= 0.0f) {
        if (pTrans <= 0.0f) return 0.0f;
        return pTrans * cosineHemispherePdf(-wiLocal.z);
    }

    Vec3f hLocal = (woLocal + wiLocal).normalized();
    float cosThetaH = hLocal.z;
    float cosThetaD = woLocal.dot(hLocal);
    if (cosThetaD <= 0.0f) return 0.0f;

    float pdfDiffuse = cosineHemispherePdf(wiLocal.z);
    float D = lobeD(hLocal, p.roughness, p.anisotropy);
    float pdfSpecular = (D * cosThetaH) / (4.0f * cosThetaD);

    float pdfCoat = 0.0f;
    if (pCoat > 0.0f) {
        float alphaC = p.clearCoatRoughness * p.clearCoatRoughness;
        float Dc = ggxD(cosThetaH, alphaC);
        pdfCoat = (Dc * cosThetaH) / (4.0f * cosThetaD);
    }

    return pDiffRefl * pdfDiffuse + pSpec * pdfSpecular + pCoat * pdfCoat;
}

} // namespace

std::shared_ptr<Image> DisneyMaterial::loadMap(const std::string& path, TextureEncoding encoding) {
    if (path.empty()) return nullptr;
    if (auto ldr = loadImageLDR(path, encoding)) {
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
    m_albedoTex = loadMap(p, TextureEncoding::SRGB);
}

void DisneyMaterial::setNormalMap(const std::string& p) {
    m_normalMap = p;
    m_normalTex = loadMap(p, TextureEncoding::Linear);
}

void DisneyMaterial::setRoughnessMap(const std::string& p) {
    m_roughnessMap = p;
    m_roughnessTex = loadMap(p, TextureEncoding::Linear);
}

void DisneyMaterial::setMetalnessMap(const std::string& p) {
    m_metalnessMap = p;
    m_metalnessTex = loadMap(p, TextureEncoding::Linear);
}

DisneyMaterial::ShadingParams DisneyMaterial::resolve(const SurfaceInteraction& si) const {
    ShadingParams p;
    p.baseColor = m_baseColor;
    p.metallic = m_metallic;
    p.roughness = m_roughness;
    p.specular = m_specular;
    p.clearCoat = m_clearCoat;
    p.clearCoatRoughness = m_clearCoatRoughness;
    p.anisotropy = m_anisotropy;
    p.sheen = m_sheen;
    p.diffuseTransmission = m_diffuseTransmission;
    p.normal = si.normal;
    p.tangent = si.tangent;

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
    Frame frame = shadingFrame(p.normal, p.tangent);
    Vec3f woLocal = frame.toLocal(wo);

    if (woLocal.z <= 0.0f) {
        return false;
    }

    float pCoat, pSpec, pDiffRefl, pTrans;
    lobeWeights(p, pCoat, pSpec, pDiffRefl, pTrans);

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
        Vec3f hLocal;
        if (p.anisotropy > 1e-4f) {
            float ax, ay;
            anisoAlpha(p.roughness, p.anisotropy, ax, ay);
            hLocal = sampleGGXAniso(Vec2f(scaledX, sample.y), ax, ay);
        } else {
            hLocal = sampleGGX(Vec2f(scaledX, sample.y), p.roughness * p.roughness);
        }
        wiLocal = reflectVec(woLocal, hLocal);
        if (wiLocal.z <= 0.0f) return false;
    } else if (xi < pCoat + pSpec + pDiffRefl) {
        float scaledX = (xi - pCoat - pSpec) / std::max(pDiffRefl, 1e-6f);
        wiLocal = cosineSampleHemisphere(Vec2f(scaledX, sample.y));
    } else {
        float scaledX = (xi - pCoat - pSpec - pDiffRefl) / std::max(pTrans, 1e-6f);
        wiLocal = cosineSampleHemisphere(Vec2f(scaledX, sample.y));
        wiLocal.z = -wiLocal.z;
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
