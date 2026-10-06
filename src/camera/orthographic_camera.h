// orthographic_camera.h — Ortografik (paralel ışınlı) kamera ve odak uzunluğu ↔ görüş açısı
// dönüşümleri (35 mm tam kare sensör varsayımıyla).
#pragma once

#include "camera/camera.h"
#include "core/math/vec.h"
#include "core/math/constants.h"
#include "core/math/utils.h"
#include <cmath>

namespace photon {

/// Tam kare (36×24 mm) sensörün yüksekliği: odak uzunluğu (mm) → dikey görüş açısı.
/// Yatay kadraj için Blender/KeyShot gibi 36 mm genişlik × en-boy oranı kullanılır.
constexpr float kSensorHeightMm = 24.0f;

// İğne deliği geometrisi: sensörün yarı yüksekliği (12 mm) ile odak uzunluğu f bir dik
// üçgen oluşturur → tan(fov/2) = (h/2)/f, yani fov = 2·atan(h / 2f). Örn. 50 mm → ≈ 27°.
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
