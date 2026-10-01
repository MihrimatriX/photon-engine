#pragma once

#include "camera/camera.h"
#include "core/math/vec.h"
#include "core/math/constants.h"
#include "core/math/utils.h"
#include <cmath>

namespace photon {

/// Full-frame sensor height used to turn a focal length in mm into a vertical FOV.
constexpr float kSensorHeightMm = 36.0f;

inline float fovDegreesFromFocalMm(float focalMm) {
    if (focalMm < 1.0f) focalMm = 1.0f;
    return rad2deg(2.0f * std::atan((kSensorHeightMm * 0.5f) / focalMm));
}

inline float focalMmFromFovDegrees(float fovDeg) {
    float t = std::tan(deg2rad(fovDeg) * 0.5f);
    if (t < 1e-4f) t = 1e-4f;
    return (kSensorHeightMm * 0.5f) / t;
}

/// Parallel rays. verticalExtent is the world-space height of the frame.
class OrthographicCamera : public Camera {
public:
    OrthographicCamera(const Vec3f& position, const Vec3f& target, const Vec3f& up,
                       float verticalExtent, float aspectRatio);

    Ray generateRay(float u, float v, const Vec2f& lensSample) const override;

private:
    Vec3f m_horizontal;
    Vec3f m_vertical;
    Vec3f m_lowerLeft;
    Vec3f m_forward;
};

} // namespace photon
