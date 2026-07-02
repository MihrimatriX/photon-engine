#include "lights/point_light.h"
#include "core/math/constants.h"

namespace photon {

LightSample PointLight::sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const {
    LightSample ls;
    Vec3f toLight = m_position - si.point;
    float distSq = toLight.lengthSquared();
    ls.distance = std::sqrt(distSq);
    
    if (ls.distance > 0.0f) {
        ls.wi = toLight / ls.distance;
        ls.Li = m_intensity / distSq;
        ls.pdf = 1.0f; // Delta light PDF
    }
    
    return ls;
}

Color3f PointLight::power() const {
    return m_intensity * 4.0f * PI;
}

} // namespace photon
