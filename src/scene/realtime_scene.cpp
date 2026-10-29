// realtime_scene.cpp — Gerçek zamanlı modun ortam pişirmesi, SH9, ışık listesi, gölge izdüşümü
// ve ton eşleme LUT'u.
#include "scene/realtime_scene.h"
#include "core/math/constants.h"
#include "lights/environment_light.h"

#include <algorithm>
#include <cmath>

namespace photon {

namespace {

/// Gerçek küresel harmonik tabanı, l ≤ 2 (shader'daki shBasis ile aynı sıra ve sabitler).
void shBasis(const Vec3f& d, float y[9]) {
    y[0] = 0.282095f;
    y[1] = 0.488603f * d.y;
    y[2] = 0.488603f * d.z;
    y[3] = 0.488603f * d.x;
    y[4] = 1.092548f * d.x * d.y;
    y[5] = 1.092548f * d.y * d.z;
    y[6] = 0.315392f * (3.0f * d.z * d.z - 1.0f);
    y[7] = 1.092548f * d.x * d.z;
    y[8] = 0.546274f * (d.x * d.x - d.y * d.y);
}

/// Işınım konvolüsyonu (kosinüs lobu) bant katsayıları: Â_0 = π, Â_1 = 2π/3, Â_2 = π/4.
constexpr float kBand[9] = {PI, 2.0f * PI / 3.0f, 2.0f * PI / 3.0f, 2.0f * PI / 3.0f,
                            PI / 4.0f, PI / 4.0f, PI / 4.0f, PI / 4.0f, PI / 4.0f};

float spotFalloff(const RealtimeLight& l, const Vec3f& fromLight) {
    if (l.type != RealtimeLight::Type::Spot || l.cosOuter < -1.0f) return 1.0f;
    const float c = fromLight.dot(l.direction);
    if (c <= l.cosOuter) return 0.0f;
    if (c >= l.cosInner) return 1.0f;
    const float t = (c - l.cosOuter) / std::max(l.cosInner - l.cosOuter, 1e-6f);
    return t * t * (3.0f - 2.0f * t);
}

/// @p p noktasında @p n normalli yüzeye düşen ışınımın parlaklığı (gölgesiz). Işık seçimi ve zemin.
float irradianceLum(const RealtimeLight& l, const Vec3f& p, const Vec3f& n) {
    const float I = std::max(0.0f, l.intensity.luminance());
    if (l.type == RealtimeLight::Type::Directional) return I * std::max(0.0f, n.dot(-l.direction));
    const Vec3f d = l.position - p;
    const float d2 = std::max(d.lengthSquared(), 1e-8f);
    const Vec3f wi = d / std::sqrt(d2);
    float k = spotFalloff(l, -wi);
    if (l.type == RealtimeLight::Type::Area) k *= std::abs(wi.dot(l.direction));
    return I * k * std::max(0.0f, n.dot(wi)) / d2;
}

Vec3f anyUp(const Vec3f& forward) { return std::abs(forward.y) > 0.99f ? Vec3f(1, 0, 0) : Vec3f(0, 1, 0); }

} // namespace

void projectIrradianceSH(const std::vector<float>& rgb, int width, int height, float out[9][3]) {
    double acc[9][3] = {};
    const double dPhi = TWO_PI / width, dTheta = PI / height;
    for (int y = 0; y < height; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
        const double dOmega = dPhi * dTheta * std::sin(v * PI);
        for (int x = 0; x < width; ++x) {
            const Vec3f d = equirectDirection((static_cast<float>(x) + 0.5f) / static_cast<float>(width), v, 0.0f);
            float b[9];
            shBasis(d, b);
            const float* c = &rgb[(static_cast<size_t>(y) * width + x) * 3];
            for (int i = 0; i < 9; ++i)
                for (int ch = 0; ch < 3; ++ch) acc[i][ch] += c[ch] * b[i] * dOmega;
        }
    }
    for (int i = 0; i < 9; ++i)
        for (int ch = 0; ch < 3; ++ch) out[i][ch] = static_cast<float>(acc[i][ch] * kBand[i]);
}

Color3f evalIrradianceSH(const float sh[9][3], const Vec3f& n) {
    float b[9];
    shBasis(n, b);
    float e[3] = {0, 0, 0};
    for (int i = 0; i < 9; ++i)
        for (int ch = 0; ch < 3; ++ch) e[ch] += sh[i][ch] * b[i];
    return Color3f(std::max(0.0f, e[0]), std::max(0.0f, e[1]), std::max(0.0f, e[2]));
}

bool extractSunFromMap(RealtimeEnvironment& env) {
    if (env.hasSun || env.width <= 0) return false;
    const int W = env.width, H = env.height;
    auto lumAt = [&](int x, int y) {
        const float* p = &env.rgb[(static_cast<size_t>(y) * W + x) * 3];
        return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
    };
    auto omega = [&](int y) { return (TWO_PI / W) * (PI / H) * std::sin((y + 0.5f) * PI / H); };
    auto dirOf = [&](int x, int y) { return equirectDirection((x + 0.5f) / W, (y + 0.5f) / H, 0.0f); };
    double sumL = 0.0, sumW = 0.0;
    int mx = 0, my = 0;
    float maxL = -1.0f;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const float l = lumAt(x, y);
            sumL += l * omega(y);
            sumW += omega(y);
            if (l > maxL) {
                maxL = l;
                mx = x;
                my = y;
            }
        }
    const float mean = static_cast<float>(sumL / std::max(sumW, 1e-12));
    if (!(maxL > 300.0f * mean) || !(mean > 0.0f)) return false;
    // Bölge: en parlak teksele 8° içindeki, ortalamanın 30 katından parlak teksellerin hepsi.
    const Vec3f peak = dirOf(mx, my);
    const float cosRegion = std::cos(8.0f * DEG_TO_RAD), cosRing = std::cos(12.0f * DEG_TO_RAD);
    const float hot = 30.0f * mean;
    const int dy = static_cast<int>(std::ceil(12.0f / 180.0f * H)) + 1;
    // Güneşin çevresindeki gök (8°–12° halkası, parlak teksel hariç): ayıklanan teksellerin yerine.
    double ring[3] = {0, 0, 0}, ringW = 0.0;
    std::vector<std::pair<int, int>> region;
    double regionOmega = 0.0;
    for (int y = std::max(0, my - dy); y <= std::min(H - 1, my + dy); ++y)
        for (int x = 0; x < W; ++x) {
            const float c = dirOf(x, y).dot(peak);
            if (c < cosRing) continue;
            const float l = lumAt(x, y);
            if (c >= cosRegion && l > hot) {
                region.emplace_back(x, y);
                regionOmega += omega(y);
            } else if (l <= hot) {
                const float* p = &env.rgb[(static_cast<size_t>(y) * W + x) * 3];
                for (int k = 0; k < 3; ++k) ring[k] += p[k] * omega(y);
                ringW += omega(y);
            }
        }
    if (regionOmega > 0.004) return false; // geniş parlak bölge: güneş değil (softbox, pencere)
    const Color3f sky = ringW > 0.0 ? Color3f(static_cast<float>(ring[0] / ringW), static_cast<float>(ring[1] / ringW),
                                              static_cast<float>(ring[2] / ringW))
                                    : Color3f(0.0f);
    Color3f E(0.0f);
    Vec3f dir(0.0f);
    for (const auto& [x, y] : region) {
        float* p = &env.rgb[(static_cast<size_t>(y) * W + x) * 3];
        const float w = omega(y);
        const Color3f excess(std::max(0.0f, p[0] - sky.r), std::max(0.0f, p[1] - sky.g), std::max(0.0f, p[2] - sky.b));
        E += excess * w;
        dir += dirOf(x, y) * (excess.luminance() * w);
        p[0] = sky.r;
        p[1] = sky.g;
        p[2] = sky.b;
    }
    if (!(dir.lengthSquared() > 0.0f) || !(E.luminance() > 0.0f)) return false;
    env.hasSun = true;
    env.sunDirection = dir.normalized();
    env.sunIrradiance = E;
    // Lekenin açısal yarıçapı: HDRI'de güneş birkaç piksele bulaşmıştır; gerçek disk 0.27°.
    env.sunAngularRadius = std::clamp(std::sqrt(static_cast<float>(regionOmega) * INV_PI), 0.0047f, 0.03f);
    return true;
}

RealtimeEnvironment bakeRealtimeEnvironment(const EnvironmentLight& env, int width, const Color3f& groundAlbedo) {
    RealtimeEnvironment out;
    out.width = std::clamp(width, 8, 8192);
    out.height = out.width / 2;
    out.rgb.resize(static_cast<size_t>(out.width) * out.height * 3);
    for (int y = 0; y < out.height; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(out.height);
        for (int x = 0; x < out.width; ++x) {
            const Color3f c = env.evalSky(equirectDirection((static_cast<float>(x) + 0.5f) / static_cast<float>(out.width), v, 0.0f));
            float* p = &out.rgb[(static_cast<size_t>(y) * out.width + x) * 3];
            p[0] = std::max(0.0f, c.r);
            p[1] = std::max(0.0f, c.g);
            p[2] = std::max(0.0f, c.b);
        }
    }
    if (env.hasSun()) {
        out.hasSun = true;
        out.sunDirection = env.sunDirection();
        out.sunIrradiance = env.sunRadiance() * (TWO_PI * (1.0f - env.sunCosMax()));
        out.sunAngularRadius = std::acos(std::clamp(env.sunCosMax(), -1.0f, 1.0f));
    } else {
        extractSunFromMap(out);
    }
    if (groundAlbedo.r >= 0.0f) {
        // Yukarı bakan zemine üst yarıkürenin (ve güneşin) verdiği ışınım; zemin bu ışıkla
        // aydınlanmış Lambert yüzey olarak alt yarıkürenin yerini alır: L = ρ·E/π.
        double e[3] = {0, 0, 0};
        const double dOmega0 = (TWO_PI / out.width) * (PI / out.height);
        for (int y = 0; y < out.height / 2; ++y) {
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(out.height);
            const double w = dOmega0 * std::sin(v * PI) * std::cos(v * PI);
            for (int x = 0; x < out.width; ++x) {
                const float* p = &out.rgb[(static_cast<size_t>(y) * out.width + x) * 3];
                for (int ch = 0; ch < 3; ++ch) e[ch] += p[ch] * w;
            }
        }
        Color3f E(static_cast<float>(e[0]), static_cast<float>(e[1]), static_cast<float>(e[2]));
        if (out.hasSun) E += out.sunIrradiance * std::max(0.0f, out.sunDirection.y);
        const Color3f L = groundAlbedo * E * INV_PI;
        for (int y = out.height / 2; y < out.height; ++y)
            for (int x = 0; x < out.width; ++x) {
                float* p = &out.rgb[(static_cast<size_t>(y) * out.width + x) * 3];
                p[0] = L.r;
                p[1] = L.g;
                p[2] = L.b;
            }
    }
    projectIrradianceSH(out.rgb, out.width, out.height, out.sh);
    return out;
}

std::vector<RealtimeLight> packRealtimeLights(const std::vector<LightDesc>& descs, const RealtimeEnvironment& env,
                                              const AABB& sceneBounds, size_t maxLights, size_t maxShadows) {
    std::vector<RealtimeLight> lights;
    for (const LightDesc& d : descs) {
        if (!d.enabled || d.intensity <= 0.0f) continue;
        RealtimeLight l;
        const Color3f L = lightEmission(d);
        Vec3f aim = d.target - d.position;
        aim = aim.lengthSquared() > 1e-12f ? aim.normalized() : Vec3f(0, -1, 0);
        l.position = d.position;
        switch (d.type) {
            case LightDesc::Type::Point:
                l.type = RealtimeLight::Type::Point;
                l.intensity = L;
                break;
            case LightDesc::Type::Spot:
                l.type = RealtimeLight::Type::Spot;
                l.direction = aim;
                l.intensity = L;
                if (d.coneAngleDeg < 179.9f) {
                    const float half = std::clamp(d.coneAngleDeg, 0.1f, 179.9f) * 0.5f * DEG_TO_RAD;
                    l.cosOuter = std::cos(half);
                    l.cosInner = std::cos(half * (1.0f - std::clamp(d.coneBlend, 0.0f, 1.0f)));
                }
                break;
            case LightDesc::Type::Area: {
                l.type = RealtimeLight::Type::Area;
                l.direction = aim;
                const float area = std::max(1e-4f, d.width) * std::max(1e-4f, d.height);
                l.intensity = L * area;
                l.radius = std::sqrt(area * INV_PI);
                break;
            }
            case LightDesc::Type::Directional:
                l.type = RealtimeLight::Type::Directional;
                l.direction = directionalTravelDir(d).normalized();
                l.intensity = L;
                break;
        }
        lights.push_back(l);
    }
    if (env.hasSun && env.sunIrradiance.luminance() > 0.0f) {
        RealtimeLight sun;
        sun.type = RealtimeLight::Type::Directional;
        sun.direction = -env.sunDirection.normalized();
        sun.intensity = env.sunIrradiance;
        sun.radius = env.sunAngularRadius;
        lights.push_back(sun);
    }

    // Sahne merkezine verdikleri ışınıma göre sırala (yüzey normali ışığa dönük kabul edilir).
    const bool hasBox = sceneBounds.pMin.x <= sceneBounds.pMax.x;
    const Vec3f c = hasBox ? sceneBounds.centroid() : Vec3f(0.0f);
    const float R = hasBox ? 0.5f * (sceneBounds.pMax - sceneBounds.pMin).length() : 1.0f;
    std::vector<std::pair<float, size_t>> order;
    for (size_t i = 0; i < lights.size(); ++i) {
        const RealtimeLight& l = lights[i];
        const Vec3f n = l.type == RealtimeLight::Type::Directional ? -l.direction : (l.position - c).normalized();
        order.emplace_back(irradianceLum(l, c, n), i);
    }
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<RealtimeLight> out;
    for (size_t k = 0; k < order.size() && out.size() < maxLights; ++k) out.push_back(lights[order[k].second]);
    // Gölge: en güçlü, sahnenin dışından bakan ışıklar (içerideki ışığın koni izdüşümü sahneyi kaplayamaz).
    size_t shadows = 0;
    const float top = order.empty() ? 0.0f : order[0].first;
    for (size_t k = 0; k < out.size() && shadows < maxShadows; ++k) {
        if (!(order[k].first > 0.0f) || order[k].first < 0.05f * top) break;
        const bool outside = out[k].type == RealtimeLight::Type::Directional || (out[k].position - c).length() > 1.05f * R;
        if (outside) {
            out[k].castsShadow = true;
            ++shadows;
        }
    }
    return out;
}

Color3f realtimeIrradiance(const std::vector<RealtimeLight>& lights, const Vec3f& p, const Vec3f& n) {
    Color3f e(0.0f);
    for (const RealtimeLight& l : lights) {
        const float lum = std::max(1e-12f, l.intensity.luminance());
        e += l.intensity * (irradianceLum(l, p, n) / lum);
    }
    return e;
}

AABB shadowReceiverBounds(const RealtimeLight& light, const AABB& box) {
    if (!(box.pMin.x <= box.pMax.x)) return box;
    AABB out = box;
    const float R = 0.5f * (box.pMax - box.pMin).length();
    const float groundY = box.pMin.y;
    for (int k = 0; k < 8; ++k) {
        const Vec3f p((k & 1) ? box.pMax.x : box.pMin.x, (k & 2) ? box.pMax.y : box.pMin.y, (k & 4) ? box.pMax.z : box.pMin.z);
        const Vec3f dir = light.type == RealtimeLight::Type::Directional ? light.direction : p - light.position;
        if (!(dir.y < -1e-6f) || p.y <= groundY) continue; // ışık köşenin altında: zemine düşmez
        const Vec3f d = dir.normalized();
        const float t = std::min((groundY - p.y) / d.y, 4.0f * R); // çok alçak ışıkta sonsuz uzamasın
        out.merge(p + d * t);
    }
    return out;
}

ShadowProjection shadowProjection(const RealtimeLight& light, const AABB& sceneBounds) {
    ShadowProjection sp;
    const AABB region = shadowReceiverBounds(light, sceneBounds);
    const bool hasBox = region.pMin.x <= region.pMax.x;
    const Vec3f c = hasBox ? region.centroid() : Vec3f(0.0f);
    const float R = std::max(1e-3f, hasBox ? 0.5f * (region.pMax - region.pMin).length() : 1.0f);
    if (light.type == RealtimeLight::Type::Directional) {
        const Vec3f dir = light.direction.normalized();
        const Vec3f eye = c - dir * (2.0f * R);
        const float extent = 2.04f * R;
        sp.ortho = true;
        sp.zNear = 0.5f * R;
        sp.zFar = 3.5f * R;
        sp.uvPerWorld = 1.0f / extent;
        sp.viewProj = Mat4f::ortho(extent, 1.0f, sp.zNear, sp.zFar) * Mat4f::lookAt(eye, c, anyUp(dir));
        return sp;
    }
    Vec3f toC = c - light.position;
    float d = toC.length();
    if (d < 1e-4f) {
        toC = light.direction;
        d = 1e-4f;
    }
    const Vec3f fwd = toC.normalized();
    const float halfAngle = std::asin(std::min(R / d, 0.999f)) * 1.04f;
    const float fov = std::clamp(2.0f * halfAngle, 1.0f * DEG_TO_RAD, 160.0f * DEG_TO_RAD);
    sp.zNear = std::max({d - R * 1.02f, d * 0.02f, 1e-4f});
    sp.zFar = d + R * 1.02f;
    sp.uvPerWorld = 1.0f / (2.0f * std::tan(0.5f * fov));
    sp.viewProj = Mat4f::perspective(fov, 1.0f, sp.zNear, sp.zFar) * Mat4f::lookAt(light.position, light.position + fwd, anyUp(fwd));
    return sp;
}

ToneLut buildToneLut(ToneMapOperator op, int size) {
    ToneLut lut;
    lut.size = std::clamp(size, 2, 128);
    const int n = lut.size;
    lut.rgb.resize(static_cast<size_t>(n) * n * n * 3);
    std::vector<float> axis(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        axis[static_cast<size_t>(i)] = std::exp2(lut.minLog2 + (lut.maxLog2 - lut.minLog2) * static_cast<float>(i) / static_cast<float>(n - 1));
    auto q = [](float v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    for (int b = 0; b < n; ++b)
        for (int g = 0; g < n; ++g)
            for (int r = 0; r < n; ++r) {
                const Color3f o = toneMap(Color3f(axis[static_cast<size_t>(r)], axis[static_cast<size_t>(g)], axis[static_cast<size_t>(b)]), op, 0.0f);
                uint8_t* p = &lut.rgb[((static_cast<size_t>(b) * n + g) * n + r) * 3];
                p[0] = q(o.r);
                p[1] = q(o.g);
                p[2] = q(o.b);
            }
    return lut;
}

Color3f sampleToneLut(const ToneLut& lut, const Color3f& hdr) {
    const int n = lut.size;
    if (n < 2) return Color3f(0.0f);
    auto coord = [&](float x) {
        const float s = (std::log2(std::max(x, 1e-30f)) - lut.minLog2) / (lut.maxLog2 - lut.minLog2);
        return std::clamp(s, 0.0f, 1.0f) * static_cast<float>(n - 1);
    };
    const float f[3] = {coord(hdr.r), coord(hdr.g), coord(hdr.b)};
    int i0[3], i1[3];
    float t[3];
    for (int k = 0; k < 3; ++k) {
        i0[k] = std::min(static_cast<int>(f[k]), n - 2);
        i1[k] = i0[k] + 1;
        t[k] = f[k] - static_cast<float>(i0[k]);
    }
    float out[3] = {0, 0, 0};
    for (int corner = 0; corner < 8; ++corner) {
        const int r = corner & 1 ? i1[0] : i0[0], g = corner & 2 ? i1[1] : i0[1], b = corner & 4 ? i1[2] : i0[2];
        const float w = (corner & 1 ? t[0] : 1 - t[0]) * (corner & 2 ? t[1] : 1 - t[1]) * (corner & 4 ? t[2] : 1 - t[2]);
        const uint8_t* p = &lut.rgb[((static_cast<size_t>(b) * n + g) * n + r) * 3];
        for (int ch = 0; ch < 3; ++ch) out[ch] += w * static_cast<float>(p[ch]) / 255.0f;
    }
    return Color3f(out[0], out[1], out[2]);
}

} // namespace photon
