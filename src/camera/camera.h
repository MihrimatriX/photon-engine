// camera.h — Soyut kamera arayüzü: ekran koordinatı (u, v) → dünya uzayında birincil ışın.
// Renderer her piksel örneği için raster (x+jitter, y+jitter) noktasını [0,1]² ekran
// koordinatına çevirir (y ters çevrilir: raster üst-sol, ekran alt-sol köken) ve buraya verir.
// Somut kameralar: iğne deliği (perspective), ince mercek (thin lens), ortografik.
#pragma once

/// @file camera.h
/// @brief Abstract Camera base class in PhotonEngine.

#include "core/math/ray.h"

namespace photon {

/// @brief Abstract base class for cameras.
class Camera {
public:
    virtual ~Camera() = default;

    /// @brief Generate a world-space ray for normalized image coordinates (u, v).
    ///
    /// @param[in] u Normalized horizontal pixel coordinate in [0, 1].
    /// @param[in] v Normalized vertical pixel coordinate in [0, 1].
    /// @param[in] lensSample 2D random sample for depth of field.
    /// @return A Ray pointing from the camera into the scene.
    virtual Ray generateRay(float u, float v, const Vec2f& lensSample) const = 0;
};

} // namespace photon
