#include "lights/environment_light.h"
#include "core/math/constants.h"
#include "core/math/utils.h"
#include "core/sampling/sampling.h"
#include <cmath>

namespace photon {

LightSample EnvironmentLight::sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const {
    LightSample ls;
    
    // Uniformly sample a direction on the sphere
    ls.wi = uniformSampleSphere(sample);
    ls.pdf = uniformSpherePdf();
    ls.Li = eval(ls.wi);
    ls.distance = INFINITY_F;
    
    return ls;
}

Color3f EnvironmentLight::eval(const Vec3f& direction) const {
    if (!m_envMap) {
        // Return a default soft gray background
        return Color3f(0.5f) * m_intensity;
    }
    
    Vec3f d = direction.normalized();
    
    // Convert direction to spherical coordinates
    float theta = std::acos(clamp(d.y, -1.0f, 1.0f)); // theta in [0, PI]
    float phi = std::atan2(-d.z, d.x) + PI + m_rotation; // phi in [0, 2*PI] + rotation
    
    // Wrap phi
    while (phi < 0.0f) phi += TWO_PI;
    while (phi >= TWO_PI) phi -= TWO_PI;
    
    float u = phi / TWO_PI;
    float v = theta / PI;
    
    int x = clamp(static_cast<int>(u * m_envMap->width()), 0, m_envMap->width() - 1);
    int y = clamp(static_cast<int>(v * m_envMap->height()), 0, m_envMap->height() - 1);
    
    return m_envMap->getPixel(x, y) * m_intensity;
}

Color3f EnvironmentLight::power() const {
    // Return average background luminance scaled by 4*PI
    if (!m_envMap) {
        return Color3f(0.5f) * m_intensity * 4.0f * PI;
    }
    
    // Simple average of a few pixels to estimate power
    Color3f avgColor = Color3f::black();
    avgColor += m_envMap->getPixel(0, 0);
    avgColor += m_envMap->getPixel(m_envMap->width() / 2, m_envMap->height() / 2);
    avgColor += m_envMap->getPixel(m_envMap->width() - 1, m_envMap->height() - 1);
    avgColor = avgColor / 3.0f;
    
    return avgColor * m_intensity * 4.0f * PI;
}

} // namespace photon
