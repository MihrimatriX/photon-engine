// quat.cpp — Kuaterniyon SLERP ve TRS ayrıştırma/birleştirme uygulaması.
#include "core/math/quat.h"
#include "core/math/constants.h"

#include <algorithm>
#include <cmath>

namespace photon {

Quatf Quatf::normalized() const {
    const float len2 = x * x + y * y + z * z + w * w;
    if (!(len2 > 1e-16f)) return Quatf::identity();
    const float inv = 1.0f / std::sqrt(len2);
    return Quatf(x * inv, y * inv, z * inv, w * inv);
}

Quatf Quatf::fromEulerDeg(const Vec3f& eulerDeg) {
    const float hx = eulerDeg.x * DEG_TO_RAD * 0.5f;
    const float hy = eulerDeg.y * DEG_TO_RAD * 0.5f;
    const float hz = eulerDeg.z * DEG_TO_RAD * 0.5f;
    const float cx = std::cos(hx), sx = std::sin(hx);
    const float cy = std::cos(hy), sy = std::sin(hy);
    const float cz = std::cos(hz), sz = std::sin(hz);
    // R = Rz * Ry * Rx
    return Quatf(
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
        cx * cy * cz + sx * sy * sz
    ).normalized();
}

Quatf Quatf::fromMat3(const Mat4f& m) {
    const float trace = m(0, 0) + m(1, 1) + m(2, 2);
    Quatf q;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m(2, 1) - m(1, 2)) / s;
        q.y = (m(0, 2) - m(2, 0)) / s;
        q.z = (m(1, 0) - m(0, 1)) / s;
    } else if (m(0, 0) > m(1, 1) && m(0, 0) > m(2, 2)) {
        const float s = std::sqrt(std::max(1e-12f, 1.0f + m(0, 0) - m(1, 1) - m(2, 2))) * 2.0f;
        q.w = (m(2, 1) - m(1, 2)) / s;
        q.x = 0.25f * s;
        q.y = (m(0, 1) + m(1, 0)) / s;
        q.z = (m(0, 2) + m(2, 0)) / s;
    } else if (m(1, 1) > m(2, 2)) {
        const float s = std::sqrt(std::max(1e-12f, 1.0f + m(1, 1) - m(0, 0) - m(2, 2))) * 2.0f;
        q.w = (m(0, 2) - m(2, 0)) / s;
        q.x = (m(0, 1) + m(1, 0)) / s;
        q.y = 0.25f * s;
        q.z = (m(1, 2) + m(2, 1)) / s;
    } else {
        const float s = std::sqrt(std::max(1e-12f, 1.0f + m(2, 2) - m(0, 0) - m(1, 1))) * 2.0f;
        q.w = (m(1, 0) - m(0, 1)) / s;
        q.x = (m(0, 2) + m(2, 0)) / s;
        q.y = (m(1, 2) + m(2, 1)) / s;
        q.z = 0.25f * s;
    }
    return q.normalized();
}

Vec3f Quatf::toEulerDeg() const {
    const Quatf q = normalized();
    const float sinr = 2.0f * (q.w * q.x + q.y * q.z);
    const float cosr = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
    const float rx = std::atan2(sinr, cosr);

    const float sinp = std::clamp(2.0f * (q.w * q.y - q.z * q.x), -1.0f, 1.0f);
    const float ry = std::asin(sinp);

    const float siny = 2.0f * (q.w * q.z + q.x * q.y);
    const float cosy = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
    const float rz = std::atan2(siny, cosy);
    return Vec3f(rx * RAD_TO_DEG, ry * RAD_TO_DEG, rz * RAD_TO_DEG);
}

Mat4f Quatf::toMat4() const {
    const Quatf q = normalized();
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    Mat4f m = Mat4f::identity();
    m(0, 0) = 1.0f - 2.0f * (yy + zz);
    m(0, 1) = 2.0f * (xy - wz);
    m(0, 2) = 2.0f * (xz + wy);
    m(1, 0) = 2.0f * (xy + wz);
    m(1, 1) = 1.0f - 2.0f * (xx + zz);
    m(1, 2) = 2.0f * (yz - wx);
    m(2, 0) = 2.0f * (xz - wy);
    m(2, 1) = 2.0f * (yz + wx);
    m(2, 2) = 1.0f - 2.0f * (xx + yy);
    return m;
}

Quatf Quatf::slerp(const Quatf& aIn, const Quatf& bIn, float t) {
    const Quatf a = aIn.normalized();
    Quatf b = bIn.normalized();
    float d = a.dot(b);
    if (d < 0.0f) {
        d = -d;
        b = Quatf(-b.x, -b.y, -b.z, -b.w);
    }
    if (d > 0.9995f) {
        return Quatf(
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t
        ).normalized();
    }
    const float theta = std::acos(std::clamp(d, -1.0f, 1.0f));
    const float sinT = std::sin(theta);
    const float wa = std::sin((1.0f - t) * theta) / sinT;
    const float wb = std::sin(t * theta) / sinT;
    return Quatf(
        a.x * wa + b.x * wb,
        a.y * wa + b.y * wb,
        a.z * wa + b.z * wb,
        a.w * wa + b.w * wb
    ).normalized();
}

Transform composeTRS(const Vec3f& pos, const Quatf& rot, const Vec3f& scale) {
    Mat4f m = rot.toMat4();
    for (int r = 0; r < 3; ++r) {
        m(r, 0) *= scale.x;
        m(r, 1) *= scale.y;
        m(r, 2) *= scale.z;
    }
    m(0, 3) = pos.x;
    m(1, 3) = pos.y;
    m(2, 3) = pos.z;
    return Transform(m);
}

Transform composeTRS(const Vec3f& pos, const Vec3f& rotEulerDeg, const Vec3f& scale) {
    return composeTRS(pos, Quatf::fromEulerDeg(rotEulerDeg), scale);
}

void decomposeTRS(const Transform& xf, Vec3f& pos, Quatf& rot, Vec3f& scale) {
    const Mat4f& m = xf.matrix();
    pos = Vec3f(m(0, 3), m(1, 3), m(2, 3));
    Vec3f c0(m(0, 0), m(1, 0), m(2, 0));
    Vec3f c1(m(0, 1), m(1, 1), m(2, 1));
    Vec3f c2(m(0, 2), m(1, 2), m(2, 2));

    float sx = c0.length();
    float sy = c1.length();
    float sz = c2.length();
    const float det = c0.dot(c1.cross(c2));
    if (det < 0.0f) sz = -sz;

    scale = Vec3f(sx, sy, sz);
    Mat4f r = Mat4f::identity();
    Vec3f x = sx > 1e-8f ? c0 / sx : Vec3f(1, 0, 0);
    Vec3f y = sy > 1e-8f ? c1 / sy : Vec3f(0, 1, 0);
    y = y - x * x.dot(y);
    if (y.lengthSquared() > 1e-12f) y = y.normalized();
    else y = std::abs(x.y) < 0.9f ? x.cross(Vec3f(0, 1, 0)).normalized() : x.cross(Vec3f(1, 0, 0)).normalized();
    const Vec3f z = x.cross(y).normalized();
    r(0, 0) = x.x; r(1, 0) = x.y; r(2, 0) = x.z;
    r(0, 1) = y.x; r(1, 1) = y.y; r(2, 1) = y.z;
    r(0, 2) = z.x; r(1, 2) = z.y; r(2, 2) = z.z;
    rot = Quatf::fromMat3(r);
}

Transform interpolateTransform(const Transform& t0, const Transform& t1, float alpha) {
    const float a = std::clamp(alpha, 0.0f, 1.0f);
    if (a <= 0.0f) return t0;
    if (a >= 1.0f) return t1;
    Vec3f p0, s0, p1, s1;
    Quatf r0, r1;
    decomposeTRS(t0, p0, r0, s0);
    decomposeTRS(t1, p1, r1, s1);
    const Vec3f p = p0 * (1.0f - a) + p1 * a;
    const Vec3f s = s0 * (1.0f - a) + s1 * a;
    const Quatf r = Quatf::slerp(r0, r1, a);
    return composeTRS(p, r, s);
}

} // namespace photon
