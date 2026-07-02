#include "materials/mirror.h"
#include "core/math/frame.h"

namespace photon {

bool Mirror::sample(const Vec3f& wo, const SurfaceInteraction& si, const Vec2f& sample,
                    Vec3f& wi, Color3f& brdf, float& pdf) const {
    Frame frame(si.normal);
    Vec3f woLocal = frame.toLocal(wo);

    if (woLocal.z <= 0.0f) {
        return false;
    }

    // Perfect specular reflection in local space
    Vec3f wiLocal(-woLocal.x, -woLocal.y, woLocal.z);
    wi = frame.toWorld(wiLocal).normalized();

    // Since it's a delta distribution, eval and pdf return 0.
    // In Monte Carlo integration: (BRDF * cosTheta) / PDF.
    // For a delta distribution, we model it by returning PDF = 1.0,
    // and BRDF = Reflectance / cosTheta.
    pdf = 1.0f;
    brdf = m_reflectance / wiLocal.z;

    return true;
}

Color3f Mirror::eval(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const {
    // Probability of matching the perfect reflection angle is zero
    return Color3f::black();
}

float Mirror::pdf(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const {
    return 0.0f;
}

} // namespace photon
