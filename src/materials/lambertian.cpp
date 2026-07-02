#include "materials/lambertian.h"
#include "core/math/frame.h"
#include "core/sampling/sampling.h"

namespace photon {

bool Lambertian::sample(const Vec3f& wo, const SurfaceInteraction& si, const Vec2f& sample,
                        Vec3f& wi, Color3f& brdf, float& pdf) const {
    // Construct local coordinate frame
    Frame frame(si.normal);
    Vec3f woLocal = frame.toLocal(wo);

    // If outgoing direction is below the hemisphere, no reflection is possible
    if (woLocal.z <= 0.0f) {
        return false;
    }

    // Cosine-weighted sampling on the hemisphere
    Vec3f wiLocal = cosineSampleHemisphere(sample);
    wi = frame.toWorld(wiLocal).normalized();

    brdf = eval(wo, wi, si);
    pdf = this->pdf(wo, wi, si);

    return true;
}

Color3f Lambertian::eval(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const {
    Frame frame(si.normal);
    float cosThetaI = frame.toLocal(wi).z;
    float cosThetaO = frame.toLocal(wo).z;

    // Both directions must be on the positive hemisphere side
    if (cosThetaI <= 0.0f || cosThetaO <= 0.0f) {
        return Color3f::black();
    }

    return m_albedo * INV_PI;
}

float Lambertian::pdf(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const {
    Frame frame(si.normal);
    float cosThetaI = frame.toLocal(wi).z;
    float cosThetaO = frame.toLocal(wo).z;

    if (cosThetaI <= 0.0f || cosThetaO <= 0.0f) {
        return 0.0f;
    }

    return cosineHemispherePdf(cosThetaI);
}

} // namespace photon
