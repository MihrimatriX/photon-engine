#pragma once

/// @file perspective_camera.h
/// @brief Pinhole perspective camera in PhotonEngine.

#include "camera/camera.h"
#include "core/math/vec.h"
#include "core/math/mat.h"

namespace photon {

/// @brief Ideal pinhole perspective camera.
class PerspectiveCamera : public Camera {
public:
    PerspectiveCamera(const Vec3f& position, const Vec3f& target, const Vec3f& up,
                      float vFovDegrees, float aspectRatio);

    Ray generateRay(float u, float v, const Vec2f& lensSample) const override;

private:
    Vec3f m_position;
    Vec3f m_horizontal;
    Vec3f m_vertical;
    Vec3f m_lowerLeft;

    Vec3f m_u, m_v, m_w; // Camera frame vectors (right, up, back)
};

} // namespace photon
