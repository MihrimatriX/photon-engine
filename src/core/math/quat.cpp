// quat.cpp — Kuaterniyon SLERP ve TRS ayrıştırma/birleştirme uygulaması.
#include "core/math/quat.h"
#include "core/math/constants.h"

#include <algorithm>
#include <cmath>

#if defined(_MSC_VER)
#pragma float_control(precise, on, push)
#endif

namespace photon {

namespace {

constexpr double kPiD = 3.14159265358979323846;
constexpr double kDegToRadD = kPiD / 180.0;
constexpr double kRadToDegD = 180.0 / kPiD;

inline double clampUnit(double v) {
    if (v < -1.0) return -1.0;
    if (v > 1.0) return 1.0;
    return v;
}

} // namespace

Quatf Quatf::normalized() const {
    const double dx = static_cast<double>(x);
    const double dy = static_cast<double>(y);
    const double dz = static_cast<double>(z);
    const double dw = static_cast<double>(w);
    const double len2 = dx * dx + dy * dy + dz * dz + dw * dw;
    if (!(len2 > 1e-16)) return Quatf::identity();
    const double inv = 1.0 / std::sqrt(len2);
    return Quatf(static_cast<float>(dx * inv), static_cast<float>(dy * inv),
                 static_cast<float>(dz * inv), static_cast<float>(dw * inv));
}

Quatf Quatf::fromEulerDeg(const Vec3f& eulerDeg) {
    const double hx = static_cast<double>(eulerDeg.x) * kDegToRadD * 0.5;
    const double hy = static_cast<double>(eulerDeg.y) * kDegToRadD * 0.5;
    const double hz = static_cast<double>(eulerDeg.z) * kDegToRadD * 0.5;
    const double cx = std::cos(hx), sx = std::sin(hx);
    const double cy = std::cos(hy), sy = std::sin(hy);
    const double cz = std::cos(hz), sz = std::sin(hz);
    // R = Rz * Ry * Rx
    const double qx = sx * cy * cz - cx * sy * sz;
    const double qy = cx * sy * cz + sx * cy * sz;
    const double qz = cx * cy * sz - sx * sy * cz;
    const double qw = cx * cy * cz + sx * sy * sz;
    const double len2 = qx * qx + qy * qy + qz * qz + qw * qw;
    if (!(len2 > 1e-16)) return Quatf::identity();
    const double inv = 1.0 / std::sqrt(len2);
    return Quatf(static_cast<float>(qx * inv), static_cast<float>(qy * inv),
                 static_cast<float>(qz * inv), static_cast<float>(qw * inv));
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
    const double qx = static_cast<double>(q.x);
    const double qy = static_cast<double>(q.y);
    const double qz = static_cast<double>(q.z);
    const double qw = static_cast<double>(q.w);

    const double sinr = 2.0 * (qw * qx + qy * qz);
    const double cosr = 1.0 - 2.0 * (qx * qx + qy * qy);
    const double rx = std::atan2(sinr, cosr);

    const double sinp = clampUnit(2.0 * (qw * qy - qz * qx));
    const double ry = std::asin(sinp);

    const double siny = 2.0 * (qw * qz + qx * qy);
    const double cosy = 1.0 - 2.0 * (qy * qy + qz * qz);
    const double rz = std::atan2(siny, cosy);
    return Vec3f(static_cast<float>(rx * kRadToDegD),
                 static_cast<float>(ry * kRadToDegD),
                 static_cast<float>(rz * kRadToDegD));
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
    const Quatf bn = bIn.normalized();
    double ax = static_cast<double>(a.x), ay = static_cast<double>(a.y);
    double az = static_cast<double>(a.z), aw = static_cast<double>(a.w);
    double bx = static_cast<double>(bn.x), by = static_cast<double>(bn.y);
    double bz = static_cast<double>(bn.z), bw = static_cast<double>(bn.w);
    const double td = static_cast<double>(t);

    double d = ax * bx + ay * by + az * bz + aw * bw;
    if (d < 0.0) {
        d = -d;
        bx = -bx; by = -by; bz = -bz; bw = -bw;
    }
    double rx, ry, rz, rw;
    if (d > 0.9995) {
        rx = ax + (bx - ax) * td;
        ry = ay + (by - ay) * td;
        rz = az + (bz - az) * td;
        rw = aw + (bw - aw) * td;
    } else {
        const double theta = std::acos(clampUnit(d));
        const double sinT = std::sin(theta);
        const double wa = std::sin((1.0 - td) * theta) / sinT;
        const double wb = std::sin(td * theta) / sinT;
        rx = ax * wa + bx * wb;
        ry = ay * wa + by * wb;
        rz = az * wa + bz * wb;
        rw = aw * wa + bw * wb;
    }
    const double len2 = rx * rx + ry * ry + rz * rz + rw * rw;
    if (!(len2 > 1e-16)) return Quatf::identity();
    const double inv = 1.0 / std::sqrt(len2);
    return Quatf(static_cast<float>(rx * inv), static_cast<float>(ry * inv),
                 static_cast<float>(rz * inv), static_cast<float>(rw * inv));
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

#if defined(_MSC_VER)
#pragma float_control(pop)
#endif
