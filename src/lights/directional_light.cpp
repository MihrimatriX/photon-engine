#include "lights/directional_light.h"
#include "core/math/constants.h"

namespace photon {

LightSample DirectionalLight::sampleLi(const SurfaceInteraction& /*si*/, const Vec2f& /*sample*/) const {
    LightSample ls;
    ls.wi = -m_direction;
    ls.Li = m_irradiance;
    ls.distance = INFINITY_F;
    ls.pdf = 1.0f;
    return ls;
}

Color3f DirectionalLight::power() const {
    // Power of directional light is infinite. Return irradiance * area of unit circle as representation.
    return m_irradiance * PI;
}

} // namespace photon
