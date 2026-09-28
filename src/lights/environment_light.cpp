#include "lights/environment_light.h"
#include "core/math/constants.h"
#include "core/math/frame.h"
#include "core/math/utils.h"
#include "core/sampling/sampling.h"
#include <cmath>

namespace photon {

LightSample EnvironmentLight::sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const {
    LightSample ls;

    // Cosine-weighted about shading normal — better NEE for diffuse product shots than uniform sphere.
    Frame frame(si.normal);
    Vec3f local = cosineSampleHemisphere(sample);
    ls.wi = frame.toWorld(local).normalized();
    float cosTheta = std::max(0.0f, ls.wi.dot(si.normal));
    ls.pdf = cosineHemispherePdf(cosTheta);
    ls.Li = eval(ls.wi);
    ls.distance = INFINITY_F;

    return ls;
}

float EnvironmentLight::pdfLi(const Vec3f& wi) const {
    // Fallback when no shading normal (miss MIS): uniform sphere pdf.
    (void)wi;
    return uniformSpherePdf();
}

float EnvironmentLight::pdfLi(const Vec3f& wi, const Vec3f& normal) const {
    float cosTheta = std::max(0.0f, wi.normalized().dot(normal.normalized()));
    return cosineHemispherePdf(cosTheta);
}

Color3f EnvironmentLight::eval(const Vec3f& direction) const {
    if (!m_envMap) {
        return Color3f(0.5f) * m_intensity;
    }

    Vec3f d = direction.normalized();

    float theta = std::acos(clamp(d.y, -1.0f, 1.0f));
    float phi = std::atan2(-d.z, d.x) + PI + m_rotation;

    while (phi < 0.0f) phi += TWO_PI;
    while (phi >= TWO_PI) phi -= TWO_PI;

    float u = phi / TWO_PI;
    float v = theta / PI;

    // Bilinear when map present (smoother IBL)
    return m_envMap->sampleBilinear(u, v) * m_intensity;
}

Color3f EnvironmentLight::power() const {
    if (!m_envMap) {
        return Color3f(0.5f) * m_intensity * 4.0f * PI;
    }

    Color3f avgColor = Color3f::black();
    avgColor += m_envMap->getPixel(0, 0);
    avgColor += m_envMap->getPixel(m_envMap->width() / 2, m_envMap->height() / 2);
    avgColor += m_envMap->getPixel(m_envMap->width() - 1, m_envMap->height() - 1);
    avgColor = avgColor / 3.0f;

    return avgColor * m_intensity * 4.0f * PI;
}

} // namespace photon
