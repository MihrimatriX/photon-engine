// orthographic_camera.cpp — Ortografik kamera: tüm ışınlar paralel, kökenleri değişir.
// Perspektif bozulma yoktur (uzak nesne küçülmez); teknik/ürün çizimleri için uygundur.
#include "camera/orthographic_camera.h"

namespace photon {

// Kamera tabanı (w = geri, u = sağ, v = yukarı) lookAt ile aynı mantıkla kurulur.
// Görüntü düzlemi kamera konumundan geçen verticalExtent × (en-boy · verticalExtent)
// boyutunda bir dikdörtgendir; (u,v) ∈ [0,1]² bu dikdörtgende bir köken noktası seçer.

OrthographicCamera::OrthographicCamera(const Vec3f& position, const Vec3f& target, const Vec3f& up,
                                       float verticalExtent, float aspectRatio) {
    Vec3f w = (position - target).normalized();
    Vec3f u = up.cross(w).normalized();
    Vec3f v = w.cross(u);
    m_forward = -w;

    float height = verticalExtent > 1e-4f ? verticalExtent : 1.0f;
    float width = aspectRatio * height;
    m_horizontal = width * u;
    m_vertical = height * v;
    m_lowerLeft = position - m_horizontal * 0.5f - m_vertical * 0.5f;
}

Ray OrthographicCamera::generateRay(float u, float v, const Vec2f&) const {
    Vec3f origin = m_lowerLeft + u * m_horizontal + v * m_vertical;
    return Ray(origin, m_forward);
}

} // namespace photon
