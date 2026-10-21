// orbit_camera.cpp — Orbit kameranın fare etkileşimi ve kadrajlama matematiği.
#include "ui/orbit_camera.h"
#include "core/math/constants.h"
#include <algorithm>
#include <cmath>

namespace photon {

Vec3f OrbitCamera::position() const {
    return Vec3f(target[0] + radius * std::sin(theta) * std::cos(phi),
                 target[1] + radius * std::cos(theta),
                 target[2] + radius * std::sin(theta) * std::sin(phi));
}

void OrbitCamera::getPosition(float out[3]) const {
    Vec3f p = position();
    out[0] = p.x;
    out[1] = p.y;
    out[2] = p.z;
}

void OrbitCamera::orbit(float dx, float dy) {
    phi += dx * 0.006f;
    theta -= dy * 0.006f;
    // θ = 0 tam tepede "yukarı" vektörüyle bakış yönü çakışır ve kamera tabanı
    // çöker; kutuplardan biraz uzak tutulur.
    theta = std::clamp(theta, 0.02f, PI - 0.02f);
}

void OrbitCamera::pan(float dx, float dy, float viewportHeightPx) {
    // Kamera tabanı: w = hedeften kameraya, u = sağ (dünya yukarısı × w), v = yukarı.
    Vec3f w = (position() - targetVec());
    const float len = w.length();
    if (len < 1e-8f) return;
    w = w / len;
    Vec3f u = Vec3f(0, 1, 0).cross(w);
    if (u.lengthSquared() < 1e-10f) u = Vec3f(1, 0, 0);
    u = u.normalized();
    Vec3f v = w.cross(u);
    // Hedef düzleminde bir pikselin dünya boyu = görüntü yüksekliği / piksel sayısı.
    const float worldH = orthographic
        ? 2.0f * std::tan(fov * DEG_TO_RAD * 0.5f) * radius
        : 2.0f * radius * std::tan(fov * DEG_TO_RAD * 0.5f);
    const float perPx = worldH / std::max(1.0f, viewportHeightPx);
    Vec3f delta = u * (-dx * perPx) + v * (dy * perPx);
    target[0] += delta.x;
    target[1] += delta.y;
    target[2] += delta.z;
}

void OrbitCamera::zoom(float wheel) {
    radius *= std::exp(-wheel * 0.12f);
    radius = std::clamp(radius, 1e-3f, 1e5f);
}

void OrbitCamera::tick(float dt) {
    if (turntable) phi += turntableSpeed * dt;
}

void OrbitCamera::frame(const AABB& box, float aspect) {
    if (box.pMin.x > box.pMax.x) return;
    const Vec3f c = box.centroid();
    const float r = std::max(1e-3f, (box.pMax - box.pMin).length() * 0.5f);
    target[0] = c.x;
    target[1] = c.y;
    target[2] = c.z;
    // Sınır küresi hem dikey hem yatay görüş açısına sığmalı.
    const float vHalf = fov * DEG_TO_RAD * 0.5f;
    const float hHalf = std::atan(std::tan(vHalf) * std::max(aspect, 0.1f));
    const float half = std::min(vHalf, hHalf);
    radius = r / std::sin(half) * 1.08f;
    if (!focusExplicit) focusDistance = radius;
}

} // namespace photon
