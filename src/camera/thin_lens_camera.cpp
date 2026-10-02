#include "camera/thin_lens_camera.h"
#include "core/math/constants.h"
#include "core/math/utils.h"
#include "core/sampling/sampling.h"
#include <cmath>

namespace photon {

ThinLensCamera::ThinLensCamera(const Vec3f& position, const Vec3f& target, const Vec3f& up,
                               float vFovDegrees, float aspectRatio, float apertureRadius, float focusDistance)
    : m_position(position), m_lensRadius(apertureRadius) {
    
    float theta = deg2rad(vFovDegrees);
    float h = std::tan(theta * 0.5f);
    float viewportHeight = 2.0f * h * focusDistance;
    float viewportWidth = aspectRatio * viewportHeight;

    m_w = (position - target).normalized();
    m_u = up.cross(m_w).normalized();
    m_v = m_w.cross(m_u);

    m_horizontal = viewportWidth * m_u;
    m_vertical = viewportHeight * m_v;
    m_lowerLeft = m_position - m_horizontal * 0.5f - m_vertical * 0.5f - focusDistance * m_w;
}

Ray ThinLensCamera::generateRay(float u, float v, const Vec2f& lensSample) const {
    // 1. Calculate point on focal plane
    Vec3f pFocus = m_lowerLeft + u * m_horizontal + v * m_vertical;

    // 2. Sample point on lens
    Vec2f lensPoint2D = uniformSampleDisk(lensSample) * m_lensRadius;
    Vec3f lensPointWorld = m_position + m_u * lensPoint2D.x + m_v * lensPoint2D.y;

    // 3. Compute ray pointing from lens to focus point
    Vec3f direction = (pFocus - lensPointWorld).normalized();

    // Setup ray (offset tMin to avoid self-intersection at lens)
    return Ray(lensPointWorld, direction);
}

} // namespace photon
