#include "lights/area_light.h"
#include "core/math/constants.h"
#include <cmath>

namespace photon {

LightSample AreaLight::sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const {
    LightSample ls;
    
    // Sample point on the light source rectangle
    Vec3f pLight = m_position + m_u * sample.x + m_v * sample.y;
    Vec3f toLight = pLight - si.point;
    
    float distSq = toLight.lengthSquared();
    ls.distance = std::sqrt(distSq);
    
    if (ls.distance > 0.0f) {
        ls.wi = toLight / ls.distance;
        
        // Check if the shading point is on the front side of the area light
        float cosThetaL = -ls.wi.dot(m_normal);
        
        if (cosThetaL > 1e-6f) {
            ls.Li = m_radiance;
            // Area PDF = 1 / Area
            // Convert to solid angle PDF: PDF_solid = PDF_area * dist^2 / cosThetaL
            ls.pdf = distSq / (m_area * cosThetaL);
        } else {
            ls.Li = Color3f::black();
            ls.pdf = 0.0f;
        }
    }
    
    return ls;
}

Color3f AreaLight::power() const {
    return m_radiance * m_area * PI;
}

} // namespace photon
