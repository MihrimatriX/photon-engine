// spot_light.cpp — Spot ışığın koni düşüşü, IES profili ve örneklenmesi.
#include "lights/spot_light.h"
#include "core/math/constants.h"

#include <algorithm>
#include <cmath>

namespace photon {

SpotLight::SpotLight(const Vec3f& position, const Vec3f& direction, const Color3f& intensity, float coneAngleDeg,
                     float blend, std::shared_ptr<const IesProfile> profile)
    : m_position(position),
      m_frame(direction.lengthSquared() > 0.0f ? direction.normalized() : Vec3f(0, -1, 0)),
      m_intensity(intensity),
      m_profile(std::move(profile)) {
    if (coneAngleDeg < 179.9f) {
        const float half = std::clamp(coneAngleDeg, 0.1f, 179.9f) * 0.5f * DEG_TO_RAD;
        m_cosOuter = std::cos(half);
        m_cosInner = std::cos(half * (1.0f - std::clamp(blend, 0.0f, 1.0f)));
    }
}

float SpotLight::falloff(const Vec3f& w) const {
    const float c = w.dot(m_frame.n);
    float k = 1.0f;
    if (m_cosOuter > -1.0f) {
        if (c <= m_cosOuter) return 0.0f;
        if (c < m_cosInner) {
            // smoothstep: kenarda türevi sıfır, halka izi bırakmaz.
            const float t = (c - m_cosOuter) / std::max(m_cosInner - m_cosOuter, 1e-6f);
            k = t * t * (3.0f - 2.0f * t);
        }
    }
    if (m_profile) {
        const float gamma = std::acos(std::clamp(c, -1.0f, 1.0f)) * RAD_TO_DEG;
        const float phi = std::atan2(w.dot(m_frame.t), w.dot(m_frame.s)) * RAD_TO_DEG;
        k *= m_profile->evaluate(gamma, phi);
    }
    return k;
}

LightSample SpotLight::sampleLi(const SurfaceInteraction& si, const Vec2f&) const {
    LightSample ls;
    const Vec3f toLight = m_position - si.point;
    const float d2 = toLight.lengthSquared();
    if (!(d2 > 0.0f)) return ls;
    ls.distance = std::sqrt(d2);
    ls.wi = toLight / ls.distance;
    const float k = falloff(-ls.wi);
    if (k <= 0.0f) return ls;
    ls.Li = m_intensity * (k / d2);
    ls.pdf = 1.0f;
    return ls;
}

Color3f SpotLight::power() const {
    // Koni içinde düzgün varsayımıyla Φ ≈ I · 2π(1 − cos(ortalama yarı açı)).
    const float cosAvg = m_cosOuter > -1.0f ? 0.5f * (m_cosOuter + m_cosInner) : -1.0f;
    return m_intensity * (TWO_PI * (1.0f - cosAvg));
}

float SpotLight::importance(const Vec3f& p, const Vec3f& n) const {
    const Vec3f d = m_position - p;
    const float d2 = std::max(d.lengthSquared(), 1e-6f);
    const Vec3f wi = d / std::sqrt(d2);
    return std::max(0.0f, m_intensity.luminance()) * falloff(-wi) * receiverCosine(n, wi) / d2;
}

} // namespace photon
