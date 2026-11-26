// quat.h — Birim kuaterniyon (Quatf), SLERP, TRS ayrıştırma/birleştirme ve hareket bulanıklığı
// (Motion Blur) için dönüşüm enterpolasyonu. Hem photon_geometry (MeshInstance) hem
// photon_scene (TransformTrack) tarafından kullanılır.
#pragma once

#include "core/math/mat.h"
#include "core/math/transform.h"
#include "core/math/vec.h"

namespace photon {

/// Birim kuaterniyon (x, y, z, w): dönüşüm enterpolasyonunda gimbal kilitsiz en kısa yay (SLERP).
struct Quatf {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    constexpr Quatf() = default;
    constexpr Quatf(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

    static Quatf identity() { return Quatf(0.0f, 0.0f, 0.0f, 1.0f); }
    static Quatf fromEulerDeg(const Vec3f& eulerDeg); ///< ZYX (roll-yaw-pitch) derece
    static Quatf fromMat3(const Mat4f& rot);          ///< Ortogonal 3×3 dönüş bloğundan

    Vec3f toEulerDeg() const;
    Mat4f toMat4() const;

    float dot(const Quatf& o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }
    Quatf normalized() const;

    /// Küresel doğrusal enterpolasyon (en kısa yol: dot < 0 ise işaret çevrilir).
    static Quatf slerp(const Quatf& a, const Quatf& b, float t);
};

/// Konum, Euler derece ve ölçekten Transform oluşturur (T · Rz · Ry · Rx · S).
Transform composeTRS(const Vec3f& pos, const Vec3f& rotEulerDeg, const Vec3f& scale);
Transform composeTRS(const Vec3f& pos, const Quatf& rot, const Vec3f& scale);

/// Afin dönüşümü (konum, birim kuaterniyon, ölçek) üçlüsüne ayırır (negatif determinant korunur).
void decomposeTRS(const Transform& xf, Vec3f& pos, Quatf& rot, Vec3f& scale);

/// İki dönüşüm arasında alpha ∈ [0, 1] anındaki dönüşüm (konum/ölçek doğrusal, dönüş SLERP).
Transform interpolateTransform(const Transform& t0, const Transform& t1, float alpha);

} // namespace photon
