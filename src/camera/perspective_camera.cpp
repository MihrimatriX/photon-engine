// perspective_camera.cpp — İğne deliği (pinhole) kamera: tüm ışınlar tek bir noktadan çıkar.
// Her şey keskin odaktadır (alan derinliği yok). Ekran → ışın eşlemesi "sanal görüntü
// düzlemi" ile yapılır (Shirley, "Ray Tracing in One Weekend" kamera kurgusu).
#include "camera/perspective_camera.h"
#include "core/math/constants.h"
#include "core/math/utils.h"
#include <cmath>

namespace photon {

PerspectiveCamera::PerspectiveCamera(const Vec3f& position, const Vec3f& target, const Vec3f& up,
                                     float vFovDegrees, float aspectRatio)
    : m_position(position) {
    
    // Kameranın 1 birim önüne bir görüntü düzlemi koyarız. Dikey görüş açısı θ ise
    // düzlemin yarı yüksekliği tan(θ/2), tam yüksekliği 2·tan(θ/2); genişlik = en-boy × yükseklik.
    // Sağ-el kuralı: kamera −w yönüne bakar (OpenGL geleneği); u = up × w sağ, v = w × u yukarı.
    float theta = deg2rad(vFovDegrees);
    float h = std::tan(theta * 0.5f);
    float viewportHeight = 2.0f * h;
    float viewportWidth = aspectRatio * viewportHeight;

    m_w = (position - target).normalized(); // Backwards vector
    m_u = up.cross(m_w).normalized();       // Right vector
    m_v = m_w.cross(m_u);                   // Up vector (already normalized since w and u are perpendicular unit vectors)

    m_horizontal = viewportWidth * m_u;
    m_vertical = viewportHeight * m_v;
    
    // We assume focal distance is 1.0 for pinhole camera viewport placement
    m_lowerLeft = m_position - m_horizontal * 0.5f - m_vertical * 0.5f - m_w;
}

// Ekran (u,v) ∈ [0,1]² → düzlemde nokta: P = alt-sol + u·yatay + v·dikey (çift doğrusal
// eşleme). Işın kamera konumundan P'ye gider. Lens örneği kullanılmaz (sonsuz küçük açıklık).
Ray PerspectiveCamera::generateRay(float u, float v, const Vec2f& /*lensSample*/) const {
    Vec3f targetPoint = m_lowerLeft + u * m_horizontal + v * m_vertical;
    Vec3f direction = (targetPoint - m_position).normalized();
    return Ray(m_position, direction);
}

} // namespace photon
