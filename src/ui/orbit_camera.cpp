#include "ui/orbit_camera.h"
#include <cmath>
#include <algorithm>

namespace photon {

void OrbitCamera::getPosition(float out[3]) const {
    out[0] = target[0] + radius * std::sin(theta) * std::cos(phi);
    out[1] = target[1] + radius * std::cos(theta);
    out[2] = target[2] + radius * std::sin(theta) * std::sin(phi);
}

void OrbitCamera::orbit(float dx, float dy) {
    phi -= dx * 0.005f;
    theta -= dy * 0.005f;
    theta = std::clamp(theta, 0.01f, 3.14f - 0.01f);
}

void OrbitCamera::pan(float dx, float dy) {
    float pos[3];
    getPosition(pos);
    float w[3] = {pos[0] - target[0], pos[1] - target[1], pos[2] - target[2]};
    float wLen = std::sqrt(w[0]*w[0] + w[1]*w[1] + w[2]*w[2]);
    if (wLen < 1e-6f) return;
    w[0] /= wLen; w[1] /= wLen; w[2] /= wLen;
    float u[3] = {-w[2], 0.0f, w[0]};
    float uLen = std::sqrt(u[0]*u[0] + u[2]*u[2]);
    if (uLen > 1e-6f) { u[0] /= uLen; u[2] /= uLen; } else { u[0]=1; u[2]=0; }
    float v[3] = {
        w[1]*u[2] - w[2]*u[1],
        w[2]*u[0] - w[0]*u[2],
        w[0]*u[1] - w[1]*u[0]
    };
    float panSpeed = 0.003f * radius;
    target[0] -= (u[0]*dx - v[0]*dy) * panSpeed;
    target[1] -= (u[1]*dx - v[1]*dy) * panSpeed;
    target[2] -= (u[2]*dx - v[2]*dy) * panSpeed;
}

void OrbitCamera::zoom(float delta) {
    radius += delta * 0.002f * radius;
    radius = std::clamp(radius, 1.0f, 5000.0f);
}

void OrbitCamera::tick(float dt) {
    if (turntable) phi += turntableSpeed * dt;
}

} // namespace photon
