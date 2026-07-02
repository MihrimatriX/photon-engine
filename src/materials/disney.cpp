#include "materials/disney.h"
#include "core/math/frame.h"
#include "core/math/utils.h"
#include "core/sampling/sampling.h"
#include <cmath>

namespace photon {

bool DisneyMaterial::sample(const Vec3f& wo, const SurfaceInteraction& si, const Vec2f& sample,
                            Vec3f& wi, Color3f& brdf, float& pdf) const {
    Frame frame(si.normal);
    Vec3f woLocal = frame.toLocal(wo);

    if (woLocal.z <= 0.0f) {
        return false;
    }

    // Probability of sampling the specular lobe vs diffuse lobe
    // For metal, it's 100% specular. Otherwise we use a 50/50 split.
    float pSpec = lerp(0.5f, 1.0f, m_metallic);

    Vec3f wiLocal;

    if (sample.x >= pSpec) {
        // Sample Diffuse (re-scale sample.x)
        float scaledX = (sample.x - pSpec) / (1.0f - pSpec);
        wiLocal = cosineSampleHemisphere(Vec2f(scaledX, sample.y));
    } else {
        // Sample Specular Lobe (GGX distribution of normals)
        float scaledX = sample.x / pSpec;
        
        float alpha = m_roughness * m_roughness;
        float theta = std::atan(alpha * std::sqrt(scaledX) / std::sqrt(1.0f - scaledX));
        float phi = TWO_PI * sample.y;

        // Microfacet normal (half vector) in local space
        Vec3f hLocal(
            std::sin(theta) * std::cos(phi),
            std::sin(theta) * std::sin(phi),
            std::cos(theta)
        );

        // Reflect woLocal about hLocal to get wiLocal
        // reflectVec expects incident vector pointing towards surface (-woLocal)
        wiLocal = reflectVec(woLocal, hLocal);

        if (wiLocal.z <= 0.0f) {
            return false;
        }
    }

    wi = frame.toWorld(wiLocal).normalized();

    brdf = eval(wo, wi, si);
    pdf = this->pdf(wo, wi, si);

    return pdf > 0.0f;
}

Color3f DisneyMaterial::eval(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const {
    Frame frame(si.normal);
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

    // ── Diffuse Lobe (Burley Diffuse) ────────────────────────
    float F90 = 0.5f + 2.0f * m_roughness * cosThetaD * cosThetaD;
    float Fo = 1.0f + (F90 - 1.0f) * std::pow(1.0f - cosThetaO, 5.0f);
    float Fi = 1.0f + (F90 - 1.0f) * std::pow(1.0f - cosThetaI, 5.0f);
    Color3f fDiffuse = m_baseColor * INV_PI * Fo * Fi * (1.0f - m_metallic);

    // ── Specular Lobe (GGX Microfacet Cook-Torrance) ─────────
    float alpha = m_roughness * m_roughness;
    float alpha2 = alpha * alpha;
    
    // NDF (D)
    float denom = cosThetaH * cosThetaH * (alpha2 - 1.0f) + 1.0f;
    float D = alpha2 / (PI * denom * denom);

    // Shadowing-Masking (G - height correlated Smith)
    auto smithG1 = [alpha2](float cosTheta) {
        float cosTheta2 = cosTheta * cosTheta;
        float tanTheta2 = std::max(0.0f, 1.0f - cosTheta2) / cosTheta2;
        return 2.0f / (1.0f + std::sqrt(1.0f + alpha2 * tanTheta2));
    };
    float G = smithG1(cosThetaO) * smithG1(cosThetaI);

    // Fresnel (F - Schlick approximation)
    // Dialectics F0 is based on specular (0.04 * specular). Metals F0 is baseColor.
    Color3f F0 = lerp(Color3f(0.04f * m_specular), m_baseColor, m_metallic);
    Color3f F = F0 + (Color3f(1.0f) - F0) * std::pow(1.0f - cosThetaD, 5.0f);

    Color3f fSpecular = (F * D * G) / (4.0f * cosThetaO * cosThetaI);

    return fDiffuse + fSpecular;
}

float DisneyMaterial::pdf(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const {
    Frame frame(si.normal);
    Vec3f woLocal = frame.toLocal(wo);
    Vec3f wiLocal = frame.toLocal(wi);

    if (woLocal.z <= 0.0f || wiLocal.z <= 0.0f) {
        return 0.0f;
    }

    Vec3f hLocal = (woLocal + wiLocal).normalized();
    float cosThetaH = hLocal.z;
    float cosThetaD = woLocal.dot(hLocal);

    float pSpec = lerp(0.5f, 1.0f, m_metallic);

    // Diffuse PDF (cosine weighted)
    float pdfDiffuse = cosineHemispherePdf(wiLocal.z);

    // Specular PDF (GGX)
    float alpha = m_roughness * m_roughness;
    float alpha2 = alpha * alpha;
    float denom = cosThetaH * cosThetaH * (alpha2 - 1.0f) + 1.0f;
    float D = alpha2 / (PI * denom * denom);
    
    float pdfSpecular = (D * cosThetaH) / (4.0f * cosThetaD);

    return (1.0f - pSpec) * pdfDiffuse + pSpec * pdfSpecular;
}

} // namespace photon
