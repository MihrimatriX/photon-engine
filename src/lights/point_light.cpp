// point_light.cpp — Nokta ışığın örneklenmesi ve toplam gücü.
#include "lights/point_light.h"
#include "core/math/constants.h"

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

} // namespace photon
