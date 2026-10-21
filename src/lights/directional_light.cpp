// directional_light.cpp — Yönlü ışığın örneklenmesi.
#include "lights/directional_light.h"
#include "core/math/constants.h"

namespace photon {

LightSample DirectionalLight::sampleLi(const SurfaceInteraction& /*si*/, const Vec2f& /*sample*/) const {
    // Her noktada aynı yön: wi = -direction. Li burada dik yüzeydeki ışınım E'dir; tahminci
    // onu |cos θ| ile çarpar (Lambert kosinüs yasası). pdf = 1: yön sabit, delta dağılım.
    // distance = ∞ → gölge ışını sahnenin tamamını test eder.
    LightSample ls;
    ls.wi = -m_direction;
    ls.Li = m_irradiance;
    ls.distance = INFINITY_F;
    ls.pdf = 1.0f;
    return ls;
}

Color3f DirectionalLight::power() const {
    // Gerçek güç sonsuzdur (sonsuz geniş paralel demet); yalnız göreli karşılaştırma için E·π verilir.
    // Power of directional light is infinite. Return irradiance * area of unit circle as representation.
    return m_irradiance * PI;
}

} // namespace photon
