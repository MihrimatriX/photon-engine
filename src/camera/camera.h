// camera.h — Soyut kamera arayüzü: ekran koordinatı (u, v) → dünya uzayında birincil ışın.
// Hareket bulanıklığı (Motion Blur) için enstantane sonu pozu (setShutterEnd) ve GPU/ışın konisi
// (Ray Cone) parametre tanımları (describeGpu, pixelConeSpread, pixelConeWidth) de buradadır.
#pragma once

/// @file camera.h
/// @brief Abstract Camera base class in PhotonEngine.

#include "core/math/ray.h"
#include "core/sampling/sampling.h"

#include <algorithm>
#include <cmath>

namespace photon {

/// Kamera parametrelerinin GPU'ya ve hareket bulanıklığı enterpolasyonuna uygun düz tanımı.
struct CameraGpuDesc {
    Vec3f position{0.0f, 0.0f, 5.0f};
    Vec3f lowerLeft{-1.0f, -1.0f, 4.0f};
    Vec3f horizontal{2.0f, 0.0f, 0.0f};
    Vec3f vertical{0.0f, 2.0f, 0.0f};
    Vec3f u{1.0f, 0.0f, 0.0f};
    Vec3f v{0.0f, 1.0f, 0.0f};
    Vec3f forward{0.0f, 0.0f, -1.0f};
    float lensRadius = 0.0f;
    int blades = 0;
    float bladeRotation = 0.0f;
    bool orthographic = false;
};

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

    /// GPU arka uçları ve kamera hareket bulanıklığı için düz kamera tanımı.
    virtual CameraGpuDesc describeGpu() const = 0;

    /// Kamera hareket bulanıklığı (Motion Blur): t = 1 (enstantane kapanışı) kamera pozu.
    void setShutterEnd(const CameraGpuDesc& endDesc) {
        m_shutterEnd = endDesc;
        m_hasShutterEnd = true;
    }
    void setShutterEnd(const Camera& endCam) { setShutterEnd(endCam.describeGpu()); }
    bool hasShutterEnd() const { return m_hasShutterEnd; }
    CameraGpuDesc describeGpuEnd() const { return m_hasShutterEnd ? m_shutterEnd : describeGpu(); }

    /// Enstantane anı @p time ∈ [0, 1] için birincil ışın (hareketli kamerada enterpolasyonlu).
    virtual Ray generateRay(float u, float v, const Vec2f& lensSample, float time) const {
        if (!m_hasShutterEnd || time <= 0.0f) {
            Ray r = generateRay(u, v, lensSample);
            r.time = std::clamp(time, 0.0f, 1.0f);
            return r;
        }
        const float t = std::clamp(time, 0.0f, 1.0f);
        const CameraGpuDesc a = describeGpu();
        const CameraGpuDesc& b = m_shutterEnd;
        const auto mix3 = [t](const Vec3f& x, const Vec3f& y) { return x * (1.0f - t) + y * t; };
        const Vec3f pos = mix3(a.position, b.position);
        const Vec3f ll = mix3(a.lowerLeft, b.lowerLeft);
        const Vec3f horiz = mix3(a.horizontal, b.horizontal);
        const Vec3f vert = mix3(a.vertical, b.vertical);

        Ray r;
        if (a.orthographic) {
            const Vec3f fwd = mix3(a.forward, b.forward).normalized();
            r = Ray(ll + horiz * u + vert * v, fwd);
        } else {
            const float radius = a.lensRadius * (1.0f - t) + b.lensRadius * t;
            Vec3f origin = pos;
            if (radius > 0.0f) {
                const Vec3f cu = mix3(a.u, b.u).normalized();
                const Vec3f cv = mix3(a.v, b.v).normalized();
                const Vec2f lp = uniformSampleDisk(lensSample) * radius;
                origin = pos + cu * lp.x + cv * lp.y;
            }
            const Vec3f target = ll + horiz * u + vert * v;
            r = Ray(origin, (target - origin).normalized());
        }
        r.time = t;
        return r;
    }

    /// Ray Cone (RTG §20) açısal yayılım hızı (rad / birim mesafe); ortografikte 0.
    virtual float pixelConeSpread(int filmHeight) const {
        if (filmHeight <= 0) return 0.0f;
        const CameraGpuDesc d = describeGpu();
        if (d.orthographic) return 0.0f;
        const Vec3f center = d.lowerLeft + d.horizontal * 0.5f + d.vertical * 0.5f;
        const float dist = std::max(1e-4f, (center - d.position).length());
        const float height = d.vertical.length();
        return std::atan(height / (dist * static_cast<float>(filmHeight)));
    }

    /// Ray Cone başlangıç genişliği (ortografikte sabit piksel boyu; perspektifte 0).
    virtual float pixelConeWidth(int filmHeight) const {
        if (filmHeight <= 0) return 0.0f;
        const CameraGpuDesc d = describeGpu();
        return d.orthographic ? d.vertical.length() / static_cast<float>(filmHeight) : 0.0f;
    }

private:
    CameraGpuDesc m_shutterEnd;
    bool m_hasShutterEnd = false;
};

} // namespace photon
