// Analitik küre: ışın-küre kesişimi (ikinci derece denklem), dışa dönük normal,
// küresel UV ve u yönündeki teğet.

#include "geometry/sphere.h"
#include "core/math/utils.h"
#include <cmath>

namespace photon {

bool Sphere::intersect(Ray& ray, SurfaceInteraction& isect) const {
    // Transform ray to sphere's local space (center at origin)
    Vec3f o = ray.origin - m_center;
    Vec3f d = ray.direction;

    // Set up quadratic coefficients
    float a = d.dot(d);
    float b = 2.0f * o.dot(d);
    float c = o.dot(o) - m_radius * m_radius;

    float t0, t1;
    if (!solveQuadratic(a, b, c, t0, t1)) {
        return false;
    }

    // Check if the intersection points are within the ray's valid range
    float tHit = t0;
    if (tHit < ray.tMin || tHit > ray.tMax) {
        tHit = t1;
        if (tHit < ray.tMin || tHit > ray.tMax) {
            return false;
        }
    }

    // Update ray's tMax so subsequent tests find closer intersections
    ray.tMax = tHit;

    // Populate intersection details
    isect.t = tHit;
    isect.point = ray.at(tHit);
    
    // Normal at intersection (pointing outwards)
    Vec3f outwardNormal = (isect.point - m_center) / m_radius;
    isect.ng = outwardNormal;
    isect.setFaceNormal(ray, outwardNormal);

    // Compute UV coordinates (spherical coordinates)
    // Map outwardNormal (unit vector)
    float theta = std::acos(clamp(outwardNormal.y, -1.0f, 1.0f)); // theta in [0, PI]
    float phi = std::atan2(-outwardNormal.z, outwardNormal.x) + PI; // phi in [0, 2*PI]
    if (phi < 0.0f) phi += TWO_PI;

    isect.uv.x = phi / TWO_PI;
    isect.uv.y = theta / PI;

    // Teğet = ∂P/∂u yönü. φ' = atan2(-z, x) olduğundan x = r cosφ', z = -r sinφ' ve
    //   ∂P/∂φ' = (-r sinφ', 0, -r cosφ') = (z, 0, -x).
    // (2π çarpanı normalize edilince kaybolur.) Eski kodda x bileşeninin işareti tersti;
    // teğet normale dik değildi, bazı noktalarda normale ters paralel oluyordu (geometry-9).
    isect.tangent = Vec3f(outwardNormal.z, 0.0f, -outwardNormal.x);
    if (isect.tangent.lengthSquared() == 0.0f) {
        // Fallback tangent
        float sign = std::copysign(1.0f, outwardNormal.z);
        float a_coeff = -1.0f / (sign + outwardNormal.z);
        float b_coeff = outwardNormal.x * outwardNormal.y * a_coeff;
        isect.tangent = Vec3f(1.0f + sign * outwardNormal.x * outwardNormal.x * a_coeff, sign * b_coeff, -sign * outwardNormal.x);
    }
    isect.tangent = isect.tangent.normalized();

    isect.material = m_material;

    return true;
}

AABB Sphere::bounds() const {
    return AABB(
        m_center - Vec3f(m_radius),
        m_center + Vec3f(m_radius)
    );
}

} // namespace photon
