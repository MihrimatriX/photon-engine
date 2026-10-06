// orbit_camera.h — Bir hedef nokta etrafında dönen etkileşimli kamera.
//
// Konum küresel koordinatlarla tutulur: hedef + yarıçap · (sinθ cosφ, cosθ, sinθ sinφ).
// θ (theta) dikey açı (0 = tam tepe), φ (phi) yatay açıdır. Fare sürüklemesi bu
// açıları değiştirir; tekerlek yarıçapı üstel olarak küçültür/büyütür.
#pragma once

#include "core/math/aabb.h"
#include "core/math/vec.h"

namespace photon {

struct OrbitCamera {
    float target[3] = {0.0f, 0.5f, 0.0f};
    float radius = 5.0f;
    float theta = 1.15f;
    float phi = 0.45f;
    float fov = 35.0f;            ///< Dikey görüş açısı (derece)
    float focalLengthMm = 38.06f; ///< 24 mm sensör yüksekliğiyle fov=35°e karşılık gelir
    bool orthographic = false;
    float aperture = 0.0f;        ///< Mercek yarıçapı (sahne birimi); 0 = alan derinliği kapalı
    float fStop = 2.8f;           ///< Kullanıcıya gösterilen f-durağı
    float focusDistance = 5.0f;
    bool focusExplicit = false;   ///< false: odak yörünge yarıçapını izler
    bool turntable = false;
    float turntableSpeed = 0.5f;  ///< rad/sn

    Vec3f targetVec() const { return Vec3f(target[0], target[1], target[2]); }
    Vec3f position() const;
    void getPosition(float out[3]) const;

    /// Fare sürüklemesi (piksel): yatay → φ, dikey → θ.
    void orbit(float dx, float dy);
    /// Ekran düzleminde kaydırma. İmlecin altındaki nokta imleçle birlikte
    /// hareket etsin diye adım, görüntü yüksekliğindeki dünya boyuna oranlanır.
    void pan(float dx, float dy, float viewportHeightPx);
    /// Tekerlek: yarıçap üstel ölçeklenir (her çentik ~%12).
    void zoom(float wheel);
    void tick(float dt);
    /// Kutuyu kadraja sığdır (hedef = merkez, yarıçap = sınır küresine göre).
    void frame(const AABB& box, float aspect);
};

/// Açıklık açık ve kullanıcı odağı elle ayarlamadıysa odak = yörünge yarıçapı.
inline float effectiveFocusDistance(const OrbitCamera& cam) {
    if (cam.aperture > 0.0f && !cam.focusExplicit) return cam.radius;
    return cam.focusDistance;
}

/// f-durağı ve odak uzunluğundan mercek yarıçapı: r = f / (2N). Sahne birimi metre
/// varsayılır (f mm → m). ponytail: sahne birimi seçimi gelince ölçek eklenir.
inline float apertureFromFStop(float focalMm, float fStop) {
    return fStop > 0.0f ? (focalMm * 0.001f) / (2.0f * fStop) : 0.0f;
}

} // namespace photon
