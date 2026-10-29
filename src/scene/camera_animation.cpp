// camera_animation.cpp — Hermite ara değerleme ve JSON.
#include "scene/camera_animation.h"

#include <algorithm>
#include <cmath>

namespace photon {

namespace {

constexpr float kSameTime = 1e-3f;

// Kübik Hermite: p0, p1 uç değerler; m0, m1 zaman birimindeki teğetler; dt aralık süresi.
template <typename T>
T hermite(const T& p0, const T& m0, const T& p1, const T& m1, float u, float dt) {
    const float u2 = u * u, u3 = u2 * u;
    const float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
    const float h10 = u3 - 2.0f * u2 + u;
    const float h01 = -2.0f * u3 + 3.0f * u2;
    const float h11 = u3 - u2;
    return p0 * h00 + m0 * (h10 * dt) + p1 * h01 + m1 * (h11 * dt);
}

// Catmull-Rom teğeti (zaman birimi başına); uçlarda sıfır (yumuşak başlangıç/bitiş).
template <typename T, typename Get>
T tangent(const std::vector<CameraKey>& k, size_t i, Get get, const T& zero) {
    if (i == 0 || i + 1 >= k.size()) return zero;
    const float dt = k[i + 1].time - k[i - 1].time;
    if (dt <= 0.0f) return zero;
    return (get(k[i + 1]) - get(k[i - 1])) * (1.0f / dt);
}

nlohmann::json vec3Json(const Vec3f& v) { return nlohmann::json::array({v.x, v.y, v.z}); }

Vec3f vec3From(const nlohmann::json& j, const char* key, const Vec3f& def) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() < 3) return def;
    return Vec3f((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
}

} // namespace

CameraKey CameraTrack::evaluate(float t) const {
    if (keys.empty()) return CameraKey{};
    if (keys.size() == 1 || t <= keys.front().time) {
        CameraKey k = keys.front();
        k.time = t;
        return k;
    }
    if (t >= keys.back().time) {
        CameraKey k = keys.back();
        k.time = t;
        return k;
    }
    // t'yi içeren aralık: keys[i].time <= t < keys[i+1].time
    const auto it = std::upper_bound(keys.begin(), keys.end(), t,
                                     [](float v, const CameraKey& k) { return v < k.time; });
    const size_t i = static_cast<size_t>(it - keys.begin()) - 1;
    const CameraKey& a = keys[i];
    const CameraKey& b = keys[i + 1];
    const float dt = b.time - a.time;
    const float u = dt > 0.0f ? (t - a.time) / dt : 0.0f;

    CameraKey out;
    out.time = t;
    // Odak ve f-durağı her zaman doğrusal: odak çekme (rack focus) belirgin ve öngörülebilir olsun.
    out.focusDistance = a.focusDistance + (b.focusDistance - a.focusDistance) * u;
    out.fStop = (a.fStop > 0.0f && b.fStop > 0.0f) ? a.fStop + (b.fStop - a.fStop) * u : (u < 0.5f ? a.fStop : b.fStop);
    if (!smooth) {
        out.eye = a.eye + (b.eye - a.eye) * u;
        out.target = a.target + (b.target - a.target) * u;
        out.fovDeg = a.fovDeg + (b.fovDeg - a.fovDeg) * u;
        return out;
    }
    auto eyeOf = [](const CameraKey& k) { return k.eye; };
    auto targetOf = [](const CameraKey& k) { return k.target; };
    auto fovOf = [](const CameraKey& k) { return k.fovDeg; };
    const Vec3f zero(0.0f);
    out.eye = hermite(a.eye, tangent(keys, i, eyeOf, zero), b.eye, tangent(keys, i + 1, eyeOf, zero), u, dt);
    out.target = hermite(a.target, tangent(keys, i, targetOf, zero), b.target, tangent(keys, i + 1, targetOf, zero), u, dt);
    out.fovDeg = hermite(a.fovDeg, tangent(keys, i, fovOf, 0.0f), b.fovDeg, tangent(keys, i + 1, fovOf, 0.0f), u, dt);
    out.fovDeg = std::clamp(out.fovDeg, 1.0f, 170.0f); // eğri aşması (overshoot) görüş açısını bozmasın
    return out;
}

void CameraTrack::setKey(const CameraKey& key) {
    for (CameraKey& k : keys)
        if (std::abs(k.time - key.time) < kSameTime) {
            k = key;
            return;
        }
    keys.push_back(key);
    sortKeys();
}

bool CameraTrack::removeKeyAt(float t) {
    for (auto it = keys.begin(); it != keys.end(); ++it)
        if (std::abs(it->time - t) < kSameTime) {
            keys.erase(it);
            return true;
        }
    return false;
}

void CameraTrack::sortKeys() {
    std::stable_sort(keys.begin(), keys.end(), [](const CameraKey& a, const CameraKey& b) { return a.time < b.time; });
}

int CameraTrack::frameCount() const {
    const float f = std::max(1.0f, fps);
    return std::max(1, static_cast<int>(std::floor(duration() * f + 0.5f)) + 1);
}

nlohmann::json cameraTrackToJson(const CameraTrack& track) {
    nlohmann::json keys = nlohmann::json::array();
    for (const CameraKey& k : track.keys)
        keys.push_back({{"t", k.time}, {"eye", vec3Json(k.eye)}, {"target", vec3Json(k.target)}, {"fov", k.fovDeg},
                        {"focus", k.focusDistance}, {"fStop", k.fStop}});
    return {{"name", track.name}, {"smooth", track.smooth}, {"fps", track.fps}, {"keys", std::move(keys)}};
}

CameraTrack cameraTrackFromJson(const nlohmann::json& j) {
    CameraTrack t;
    if (!j.is_object()) return t;
    t.name = j.value("name", t.name);
    t.smooth = j.value("smooth", true);
    t.fps = std::clamp(j.value("fps", 30.0f), 1.0f, 240.0f);
    for (const auto& kj : j.value("keys", nlohmann::json::array())) {
        if (!kj.is_object()) continue;
        CameraKey k;
        k.time = kj.value("t", 0.0f);
        k.eye = vec3From(kj, "eye", k.eye);
        k.target = vec3From(kj, "target", k.target);
        k.fovDeg = std::clamp(kj.value("fov", k.fovDeg), 1.0f, 170.0f);
        k.focusDistance = std::max(0.0f, kj.value("focus", 0.0f));
        k.fStop = std::max(0.0f, kj.value("fStop", 0.0f));
        if (!std::isfinite(k.time)) continue;
        t.keys.push_back(k);
    }
    t.sortKeys();
    return t;
}

} // namespace photon
