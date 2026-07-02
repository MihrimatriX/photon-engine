#include "materials/dielectric.h"
#include "core/math/frame.h"
#include "core/math/utils.h"

namespace photon {

bool Dielectric::sample(const Vec3f& wo, const SurfaceInteraction& si, const Vec2f& sample,
                        Vec3f& wi, Color3f& brdf, float& pdf) const {
    Frame frame(si.normal);
    Vec3f woLocal = frame.toLocal(wo);

    bool entering = woLocal.z > 0.0f;
    float etaI = 1.0f;
    float etaT = m_ior;
    if (!entering) {
        std::swap(etaI, etaT);
    }

    // Evaluate Fresnel reflectance
    float fr = fresnelDielectric(woLocal.z, etaI, etaT);

    // Sample reflection or refraction
    if (sample.x < fr) {
        // Reflection
        Vec3f wiLocal(-woLocal.x, -woLocal.y, woLocal.z);
        wi = frame.toWorld(wiLocal).normalized();

        // PDF is Fresnel coefficient (due to selection probability)
        pdf = fr;
        brdf = m_tint * fr / std::abs(wiLocal.z);
    } else {
        // Refraction
        float eta = etaI / etaT;
        Vec3f nLocal(0.0f, 0.0f, entering ? 1.0f : -1.0f);
        Vec3f wiLocal;

        // refractVec expects incident vector pointing toward the surface (-woLocal)
        if (!refractVec(-woLocal, nLocal, eta, wiLocal)) {
            // Total internal reflection (should not occur since we check Fresnel first and it returned 1.0,
            // but handle as fallback)
            Vec3f reflLocal(-woLocal.x, -woLocal.y, woLocal.z);
            wi = frame.toWorld(reflLocal).normalized();
            pdf = 1.0f;
            brdf = m_tint / std::abs(reflLocal.z);
            return true;
        }

        wi = frame.toWorld(wiLocal).normalized();
        pdf = 1.0f - fr;

        // Radiance scaling by (1 / eta^2) for refraction (non-reciprocal BSDF)
        brdf = m_tint * (1.0f - fr) / (std::abs(wiLocal.z) * eta * eta);
    }

    return true;
}

Color3f Dielectric::eval(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const {
    return Color3f::black(); // Delta distribution
}

float Dielectric::pdf(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const {
    return 0.0f;
}

} // namespace photon
