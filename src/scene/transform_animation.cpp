// transform_animation.cpp — TransformTrack anahtar kare değerlendirmesi (Catmull-Rom + SLERP).
#include "scene/transform_animation.h"

#include <algorithm>
#include <cmath>

namespace photon {

namespace {

Vec3f catmullRom(const Vec3f& p0, const Vec3f& p1, const Vec3f& p2, const Vec3f& p3, float t) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return ((p1 * 2.0f) +
            (p2 - p0) * t +
            (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 +
            (-p0 + p1 * 3.0f - p2 * 3.0f + p3) * t3) * 0.5f;
}

} // namespace

void TransformTrack::setKey(const TransformKey& k) {
    for (TransformKey& cur : keys) {
        if (std::abs(cur.time - k.time) < 1e-3f) {
            cur = k;
            return;
        }
    }
    keys.push_back(k);
    std::sort(keys.begin(), keys.end(), [](const TransformKey& a, const TransformKey& b) { return a.time < b.time; });
}

bool TransformTrack::removeKeyAt(float time, float eps) {
    for (auto it = keys.begin(); it != keys.end(); ++it) {
        if (std::abs(it->time - time) <= eps) {
            keys.erase(it);
            return true;
        }
    }
    return false;
}

Transform TransformTrack::evaluate(float t) const {
    if (keys.empty()) return Transform{};
    if (keys.size() == 1 || t <= keys.front().time) {
        const auto& k = keys.front();
        return composeTRS(k.pos, k.rotEulerDeg, k.scale);
    }
    if (t >= keys.back().time) {
        const auto& k = keys.back();
        return composeTRS(k.pos, k.rotEulerDeg, k.scale);
    }
    size_t i1 = 0;
    while (i1 + 1 < keys.size() && keys[i1 + 1].time < t) ++i1;
    const size_t i2 = std::min(i1 + 1, keys.size() - 1);
    const float dt = std::max(1e-6f, keys[i2].time - keys[i1].time);
    const float u = std::clamp((t - keys[i1].time) / dt, 0.0f, 1.0f);

    const Quatf q1 = Quatf::fromEulerDeg(keys[i1].rotEulerDeg);
    const Quatf q2 = Quatf::fromEulerDeg(keys[i2].rotEulerDeg);
    const Quatf q = Quatf::slerp(q1, q2, u);

    Vec3f pos, scale;
    if (smooth && keys.size() >= 3) {
        const size_t i0 = i1 > 0 ? i1 - 1 : i1;
        const size_t i3 = i2 + 1 < keys.size() ? i2 + 1 : i2;
        pos = catmullRom(keys[i0].pos, keys[i1].pos, keys[i2].pos, keys[i3].pos, u);
        scale = catmullRom(keys[i0].scale, keys[i1].scale, keys[i2].scale, keys[i3].scale, u);
    } else {
        pos = keys[i1].pos * (1.0f - u) + keys[i2].pos * u;
        scale = keys[i1].scale * (1.0f - u) + keys[i2].scale * u;
    }
    return composeTRS(pos, q, scale);
}

} // namespace photon
