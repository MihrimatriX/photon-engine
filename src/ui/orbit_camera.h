#pragma once

namespace photon {

struct OrbitCamera {
    float target[3] = {278.0f, 273.0f, 0.0f};
    float radius = 12.0f;
    float theta = 1.57f;
    float phi = 0.0f;
    float fov = 40.0f;
    float focalLengthMm = 49.45f; ///< Matches fov when the sensor height is 36mm.
    bool orthographic = false;
    float aperture = 0.0f;
    float focusDistance = 5.5f;
    bool focusExplicit = false; ///< False: thin-lens focus tracks orbit radius.
    bool turntable = false;
    float turntableSpeed = 0.5f;

    void getPosition(float out[3]) const;
    void orbit(float dx, float dy);
    void pan(float dx, float dy);
    void zoom(float delta);
    void tick(float dt);
};

/// Aperture open and the user has not dragged focus: focus at the orbit distance.
inline float effectiveFocusDistance(const OrbitCamera& cam) {
    if (cam.aperture > 0.0f && !cam.focusExplicit) return cam.radius;
    return cam.focusDistance;
}

} // namespace photon
