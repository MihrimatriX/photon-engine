// thin_lens_camera.h — İnce mercek yaklaşımıyla alan derinliği (DoF) olan perspektif kamera.
// apertureRadius = mercek açıklığının yarıçapı (büyüdükçe bulanıklık artar),
// focusDistance = keskin görünen düzlemin kameraya uzaklığı.
#pragma once

/// @file thin_lens_camera.h
/// @brief Perspective camera with depth of field (thin lens approximation) in PhotonEngine.

#include "camera/camera.h"
#include "core/math/vec.h"

namespace photon {

/// @brief Perspective camera with finite aperture, simulating depth of field.
class ThinLensCamera : public Camera {
public:
    ThinLensCamera(const Vec3f& position, const Vec3f& target, const Vec3f& up,
                   float vFovDegrees, float aspectRatio, float apertureRadius, float focusDistance);

    Ray generateRay(float u, float v, const Vec2f& lensSample) const override;

private:
    Vec3f m_position;
    Vec3f m_horizontal;
    Vec3f m_vertical;
    Vec3f m_lowerLeft;

    Vec3f m_u, m_v, m_w;
    float m_lensRadius;
};

} // namespace photon
