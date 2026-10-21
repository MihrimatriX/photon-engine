// mirror.cpp — Delta yansıma: wi = yansıma(wo), ağırlık = reflectance.
#include "materials/mirror.h"
#include "core/math/frame.h"

namespace photon {

bool Mirror::sample(const Vec3f& wo, const SurfaceInteraction& si, const Vec2f& /*sample*/,
                    Vec3f& wi, Color3f& brdf, float& pdf) const {
    Frame frame(si.normal);
    Vec3f woLocal = frame.toLocal(wo);

    if (woLocal.z <= 0.0f) {
        return false;
    }

    // Perfect specular reflection in local space
    // Yerel uzayda normal = +z: yansıma x, y bileşenlerini ters çevirir, z'yi korur
    // (dünya uzayında wr = -wo + 2(wo·n)n ile aynı).
    Vec3f wiLocal(-woLocal.x, -woLocal.y, woLocal.z);
    wi = frame.toWorld(wiLocal).normalized();

    // Since it's a delta distribution, eval and pdf return 0.
    // In Monte Carlo integration: (BRDF * cosTheta) / PDF.
    // For a delta distribution, we model it by returning PDF = 1.0,
    // and BRDF = Reflectance / cosTheta.
    // Delta BRDF: f = R · δ(ω - ωr) / |cos θ|. Integrator f·|cos|/pdf hesapladığı için
    // brdf = R / cos ve pdf = 1 verilir → ağırlık tam R olur. pdf = 1 bir olasılık kütlesidir.
    pdf = 1.0f;
    brdf = m_reflectance / wiLocal.z;

    return true;
}

Color3f Mirror::eval(const Vec3f& /*wo*/, const Vec3f& /*wi*/, const SurfaceInteraction& /*si*/) const {
    // Probability of matching the perfect reflection angle is zero
    return Color3f::black();
}

float Mirror::pdf(const Vec3f& /*wo*/, const Vec3f& /*wi*/, const SurfaceInteraction& /*si*/) const {
    return 0.0f;
}

} // namespace photon
