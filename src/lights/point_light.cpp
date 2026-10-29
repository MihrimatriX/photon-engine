// point_light.cpp — Nokta ışığın örneklenmesi ve toplam gücü.
#include "lights/point_light.h"
#include "core/math/constants.h"
#include <algorithm>
#include <cmath>

namespace photon {

LightSample PointLight::sampleLi(const SurfaceInteraction& si, const Vec2f& /*sample*/) const {
    LightSample ls;
    Vec3f toLight = m_position - si.point;
    float distSq = toLight.lengthSquared();
    ls.distance = std::sqrt(distSq);
    
    if (ls.distance > 0.0f) {
        ls.wi = toLight / ls.distance;
        // Şiddet I (W/sr) → ters kare yasası: Li = I / r². Delta ışıkta bu bir radyans değil,
        // tahmincide |cos θ| ile çarpılınca ışınımı (irradiance E = I·cos θ / r²) veren katkıdır.
        ls.Li = m_intensity / distSq;
        ls.pdf = 1.0f; // Delta light PDF
    }
    
    return ls;
}

Color3f PointLight::power() const {
    // Toplam güç Φ = ∮ I dω = I · 4π (izotropik kaynak, tüm küre).
    return m_intensity * 4.0f * PI;
}

bool PointLight::bounds(LightBounds& out) const {
    const float I = std::max(0.0f, m_intensity.luminance());
    if (!(I > 0.0f)) return false;
    out.bounds = AABB(m_position);
    out.w = Vec3f(0, 1, 0);
    out.phi = PI * I; // alan ışıklarıyla aynı ölçü: π × (şiddet ≈ ışınım · uzaklık²)
    out.cosThetaO = -1.0f; // her yöne
    out.cosThetaE = 0.0f;
    out.twoSided = false;
    return true;
}

float PointLight::importance(const Vec3f& p, const Vec3f& n) const {
    const Vec3f d = m_position - p;
    const float d2 = std::max(d.lengthSquared(), 1e-6f);
    return std::max(0.0f, m_intensity.luminance()) * receiverCosine(n, d / std::sqrt(d2)) / d2;
}

} // namespace photon
