// camera_keys.h — Kamera animasyonu anahtarı ↔ yörünge kamerası dönüşümü.
//
// Uygulama kamerayı yörünge (hedef, yarıçap, θ, φ) olarak tutar; anahtar kareler göz ve hedef
// noktası olarak saklanır (DCC kameraları da böyle gelir). Dönüşüm kayıpsızdır; yuvarlanma (roll)
// yoktur, yukarı her zaman +Y.
#pragma once

#include "camera/orthographic_camera.h"
#include "scene/camera_animation.h"
#include "ui/orbit_camera.h"

#include <algorithm>
#include <cmath>

namespace photon {

/// Anahtardaki göz/hedef/görüş açısı/odak → yörünge kamerası. Anahtarda olmayan alanlar
/// (bokeh kanatları, ortografik) @p base'den kalır. fStop 0 ise alan derinliği kapanır.
inline OrbitCamera orbitFromKey(const CameraKey& k, const OrbitCamera& base) {
    OrbitCamera c = base;
    const Vec3f d = k.eye - k.target;
    const float r = std::max(1e-4f, d.length());
    c.target[0] = k.target.x;
    c.target[1] = k.target.y;
    c.target[2] = k.target.z;
    c.radius = r;
    c.theta = std::acos(std::clamp(d.y / r, -1.0f, 1.0f));
    c.phi = std::atan2(d.z, d.x);
    c.fov = std::clamp(k.fovDeg, 1.0f, 170.0f);
    c.focalLengthMm = focalMmFromFovDegrees(c.fov);
    if (k.fStop > 0.0f) {
        c.fStop = k.fStop;
        c.aperture = apertureFromFStop(c.focalLengthMm, c.fStop);
    } else {
        c.aperture = 0.0f;
    }
    c.focusExplicit = k.focusDistance > 0.0f;
    if (c.focusExplicit) c.focusDistance = k.focusDistance;
    return c;
}

/// Yörünge kamerası → anahtar (zaman @p time).
inline CameraKey keyFromOrbit(const OrbitCamera& c, float time) {
    CameraKey k;
    k.time = time;
    k.eye = c.position();
    k.target = c.targetVec();
    k.fovDeg = c.fov;
    k.fStop = c.aperture > 0.0f ? c.fStop : 0.0f;
    k.focusDistance = (c.aperture > 0.0f && c.focusExplicit) ? c.focusDistance : 0.0f;
    return k;
}

} // namespace photon
