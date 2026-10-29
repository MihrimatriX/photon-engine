// Gerçek zamanlı modun CPU hazırlığı (realtime_scene): GPU'nun gösterdiği, ışın izleyicinin
// sahnesiyle aynı mı?
//   - SH9 ışınımı: düzgün ortamda her yönde πL; kosinüs lobunda analitik değere %3 yakın;
//   - pişirilen ortam haritası her teksel merkezinde EnvironmentLight::evalSky'ın kendisi
//     (döndürme ve yoğunluk dahil); güneş diski haritada yok, ışınımı ayrı bir yönlü ışıkta;
//   - zemin açıkken alt yarıküre aydınlanmış zemin rengi (ρ·E/π);
//   - ışık listesi: Kelvin rengi, alan ışığında L·A şiddeti (ışın izleyicinin AreaLight'ının
//     Monte Carlo ışınımıyla uyuşur), spot konisi, yönlü ışığın gidiş yönü, gölgeyi sahnenin
//     dışındaki en güçlü ışık alır;
//   - gölge izdüşümü sahne kutusunun 8 köşesini NDC küpünün içine alır;
//   - ton eşleme LUT'u (64³), viewport'un toneMap'inden ekran değerinde en çok ~0.02 sapar.
#include <gtest/gtest.h>
#include "scene/realtime_scene.h"
#include "core/image/image.h"
#include "core/math/constants.h"
#include "core/random/rng.h"
#include "lights/environment_light.h"
#include "lights/light.h"
#include "geometry/surface_interaction.h"

#include <cmath>
#include <memory>

using namespace photon;

namespace {

std::vector<float> fillEnv(int w, int h, float (*f)(const Vec3f&)) {
    std::vector<float> rgb(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const Vec3f d = equirectDirection((x + 0.5f) / w, (y + 0.5f) / h, 0.0f);
            const float v = f(d);
            for (int c = 0; c < 3; ++c) rgb[(static_cast<size_t>(y) * w + x) * 3 + c] = v;
        }
    return rgb;
}

const Vec3f kDirs[] = {Vec3f(0, 1, 0), Vec3f(0, -1, 0), Vec3f(1, 0, 0), Vec3f(0, 0, -1),
                       Vec3f(0.3f, 0.8f, -0.52f).normalized(), Vec3f(-0.7f, -0.2f, 0.68f).normalized()};

} // namespace

TEST(RealtimeSH, UniformEnvironmentGivesPiL) {
    const auto rgb = fillEnv(256, 128, [](const Vec3f&) { return 1.0f; });
    float sh[9][3];
    projectIrradianceSH(rgb, 256, 128, sh);
    for (const Vec3f& n : kDirs) EXPECT_NEAR(evalIrradianceSH(sh, n).g, PI, 2e-3f);
}

TEST(RealtimeSH, CosineLobeMatchesAnalyticIrradiance) {
    // L(ω) = max(0, ω_y): yukarı bakan yüzeye ∫ cos² dω = 2π/3, aşağı bakana 0.
    const auto rgb = fillEnv(512, 256, [](const Vec3f& d) { return std::max(0.0f, d.y); });
    float sh[9][3];
    projectIrradianceSH(rgb, 512, 256, sh);
    EXPECT_NEAR(evalIrradianceSH(sh, Vec3f(0, 1, 0)).r, 2.0f * PI / 3.0f, 0.03f * 2.0f * PI / 3.0f);
    EXPECT_LT(evalIrradianceSH(sh, Vec3f(0, -1, 0)).r, 0.05f);
    // Yatay yüzey: ∫ max(0,y)·max(0,x) dω = ∫sin³θ dθ · ∫cosφ sinφ dφ = 4/3 · 1/2 = 2/3.
    EXPECT_NEAR(evalIrradianceSH(sh, Vec3f(1, 0, 0)).r, 2.0f / 3.0f, 0.03f * 2.0f / 3.0f);
}

TEST(RealtimeEnvironment, BakeMatchesPathTracerEnvironment) {
    auto img = std::make_shared<Image>(64, 32);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 64; ++x) img->setPixel(x, y, Color3f(x / 64.0f, y / 32.0f, ((x * 7 + y * 3) % 11) / 11.0f));
    const EnvironmentLight env(img, 0.7f, 2.5f);
    const RealtimeEnvironment rt = bakeRealtimeEnvironment(env, 128);
    ASSERT_EQ(rt.width, 128);
    ASSERT_EQ(rt.height, 64);
    EXPECT_FALSE(rt.hasSun);
    for (const auto& [x, y] : {std::pair{0, 0}, std::pair{17, 40}, std::pair{127, 63}, std::pair{64, 31}}) {
        const Vec3f d = equirectDirection((x + 0.5f) / 128.0f, (y + 0.5f) / 64.0f, 0.0f);
        const Color3f want = env.eval(d);
        const float* got = &rt.rgb[(static_cast<size_t>(y) * 128 + x) * 3];
        EXPECT_NEAR(got[0], want.r, 1e-5f);
        EXPECT_NEAR(got[1], want.g, 1e-5f);
        EXPECT_NEAR(got[2], want.b, 1e-5f);
    }
}

TEST(RealtimeEnvironment, SunBecomesDirectionalLightNotTexels) {
    auto img = std::make_shared<Image>(32, 16);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 32; ++x) img->setPixel(x, y, Color3f(0.2f));
    EnvironmentLight env(img, 0.0f, 1.0f);
    const Vec3f sunDir = Vec3f(0.3f, 0.6f, -0.2f).normalized();
    const float cosMax = std::cos(0.27f * DEG_TO_RAD);
    env.setSun(sunDir, cosMax, Color3f(5e4f, 4.5e4f, 4e4f));
    const RealtimeEnvironment rt = bakeRealtimeEnvironment(env, 256);
    ASSERT_TRUE(rt.hasSun);
    const Vec2f uv = directionToEquirect(sunDir, 0.0f);
    const int sx = static_cast<int>(uv.x * 256), sy = static_cast<int>(uv.y * 128);
    EXPECT_NEAR(rt.rgb[(static_cast<size_t>(sy) * 256 + sx) * 3], 0.2f, 1e-4f); // disk haritada yok
    const float omega = TWO_PI * (1.0f - cosMax);
    EXPECT_NEAR(rt.sunIrradiance.r, 5e4f * omega, 5e4f * omega * 1e-4f);
    EXPECT_NEAR(rt.sunDirection.dot(sunDir), 1.0f, 1e-6f);
    // Işık listesinde gölgeli bir yönlü ışık olur; gidiş yönü güneşten uzağa.
    const auto lights = packRealtimeLights({}, rt, AABB(Vec3f(-1), Vec3f(1)));
    ASSERT_EQ(lights.size(), 1u);
    EXPECT_EQ(lights[0].type, RealtimeLight::Type::Directional);
    EXPECT_NEAR(lights[0].direction.dot(-sunDir), 1.0f, 1e-5f);
    EXPECT_TRUE(lights[0].castsShadow);
}

TEST(RealtimeEnvironment, HdriSunIsExtractedIntoAShadowedLight) {
    // Düzgün gök (0.5) + 3×3 teksellik çok parlak leke: açık gök HDRI'sindeki güneş.
    RealtimeEnvironment env;
    env.width = 512;
    env.height = 256;
    env.rgb.assign(static_cast<size_t>(512) * 256 * 3, 0.5f);
    const int sx = 300, sy = 60;
    double eExpected = 0.0;
    Vec3f dirExpected(0.0f);
    for (int y = sy - 1; y <= sy + 1; ++y)
        for (int x = sx - 1; x <= sx + 1; ++x) {
            for (int c = 0; c < 3; ++c) env.rgb[(static_cast<size_t>(y) * 512 + x) * 3 + c] = 2e4f;
            const double w = (TWO_PI / 512) * (PI / 256) * std::sin((y + 0.5) * PI / 256);
            eExpected += (2e4 - 0.5) * w;
            dirExpected += equirectDirection((x + 0.5f) / 512, (y + 0.5f) / 256, 0.0f) * static_cast<float>(w);
        }
    ASSERT_TRUE(extractSunFromMap(env));
    EXPECT_NEAR(env.sunDirection.dot(dirExpected.normalized()), 1.0f, 1e-5f);
    EXPECT_NEAR(env.sunIrradiance.g, eExpected, eExpected * 1e-3);
    EXPECT_GE(env.sunAngularRadius, 0.0047f);
    float maxV = 0.0f;
    for (const float v : env.rgb) maxV = std::max(maxV, v);
    EXPECT_NEAR(maxV, 0.5f, 1e-4f); // leke haritadan çıktı: enerji iki kez sayılmaz
    EXPECT_FALSE(extractSunFromMap(env)); // ikinci kez: güneş zaten var
}

TEST(RealtimeEnvironment, StudioSoftboxIsNotMistakenForSun) {
    // Stüdyo HDRI'si: ortalamanın ~40 katı, geniş (20°×10°) bir softbox — güneş değil.
    RealtimeEnvironment env;
    env.width = 256;
    env.height = 128;
    env.rgb.assign(static_cast<size_t>(256) * 128 * 3, 0.3f);
    for (int y = 30; y < 37; ++y)
        for (int x = 100; x < 114; ++x)
            for (int c = 0; c < 3; ++c) env.rgb[(static_cast<size_t>(y) * 256 + x) * 3 + c] = 40.0f;
    EXPECT_FALSE(extractSunFromMap(env));
    // Çok parlak ama geniş leke de güneş sayılmaz.
    for (int y = 30; y < 37; ++y)
        for (int x = 100; x < 114; ++x)
            for (int c = 0; c < 3; ++c) env.rgb[(static_cast<size_t>(y) * 256 + x) * 3 + c] = 5e4f;
    EXPECT_FALSE(extractSunFromMap(env));
}

TEST(RealtimeEnvironment, GroundReplacesLowerHemisphere) {
    const EnvironmentLight env(nullptr, 0.0f, 2.0f); // düzgün L = 0.5·2 = 1
    const RealtimeEnvironment rt = bakeRealtimeEnvironment(env, 128, Color3f(0.5f, 0.25f, 0.1f));
    // Zemine düşen E = π·1 → zemin radyansı ρ·E/π = ρ.
    const float* low = &rt.rgb[(static_cast<size_t>(60) * 128 + 5) * 3];
    EXPECT_NEAR(low[0], 0.5f, 5e-3f);
    EXPECT_NEAR(low[1], 0.25f, 5e-3f);
    EXPECT_NEAR(low[2], 0.1f, 5e-3f);
    const float* high = &rt.rgb[(static_cast<size_t>(3) * 128 + 5) * 3];
    EXPECT_NEAR(high[0], 1.0f, 1e-5f);
    // Aşağı bakan yüzey zeminden ρ·π alır.
    EXPECT_NEAR(evalIrradianceSH(rt.sh, Vec3f(0, -1, 0)).r, 0.5f * PI, 0.05f);
}

TEST(RealtimeLights, SameRadiometryAsPathTracer) {
    LightDesc area;
    area.type = LightDesc::Type::Area;
    area.position = Vec3f(0.0f, 4.0f, 0.0f);
    area.target = Vec3f(0.0f, 0.0f, 0.0f);
    area.width = 0.6f;
    area.height = 0.4f;
    area.intensity = 30.0f;
    area.temperatureK = 3200.0f;
    const auto rt = packRealtimeLights({area}, RealtimeEnvironment{}, AABB(Vec3f(-1), Vec3f(1)));
    ASSERT_EQ(rt.size(), 1u);
    EXPECT_EQ(rt[0].type, RealtimeLight::Type::Area);
    const Color3f k = temperatureColor(3200.0f);
    EXPECT_NEAR(rt[0].intensity.r, k.r * 30.0f * 0.24f, 1e-3f);
    EXPECT_NEAR(rt[0].direction.y, -1.0f, 1e-6f);
    EXPECT_NEAR(rt[0].radius, std::sqrt(0.24f / PI), 1e-5f);

    // Işın izleyicinin alan ışığının bir noktaya verdiği ışınım (Monte Carlo) ile gerçek zamanlı
    // modelin I·|cosθ_l|·cosθ_r/d² değeri, kaynak küçükken aynı olmalı.
    const auto pt = buildLights({area});
    ASSERT_EQ(pt.size(), 1u);
    const Vec3f p(0.8f, 0.0f, 0.3f), n(0, 1, 0);
    SurfaceInteraction si;
    si.point = p;
    si.normal = n;
    double sum = 0.0;
    const int N = 64;
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) {
            const LightSample s = pt[0]->sampleLi(si, Vec2f((i + 0.5f) / N, (j + 0.5f) / N));
            if (s.isValid()) sum += s.Li.luminance() * std::max(0.0f, s.wi.dot(n)) / s.pdf;
        }
    const double ePath = sum / (N * N);
    const Vec3f toL = rt[0].position - p;
    const float d2 = toL.lengthSquared();
    const Vec3f wi = toL / std::sqrt(d2);
    const double eRt = rt[0].intensity.luminance() * std::abs(wi.dot(rt[0].direction)) * wi.dot(n) / d2;
    EXPECT_NEAR(eRt / ePath, 1.0, 0.02);
}

TEST(RealtimeLights, ConesShadowCasterAndOrder) {
    LightDesc spot;
    spot.type = LightDesc::Type::Spot;
    spot.position = Vec3f(0, 6, 0);
    spot.target = Vec3f(0, 0, 0);
    spot.coneAngleDeg = 40.0f;
    spot.coneBlend = 0.5f;
    spot.intensity = 400.0f;
    LightDesc inner; // kutunun içinde güçlü nokta ışık: en parlak ama gölge alamaz
    inner.type = LightDesc::Type::Point;
    inner.position = Vec3f(0.2f, 0.2f, 0.2f);
    inner.intensity = 2.0f; // merkezde 2/0.12 ≈ 16.7 > spotun 400/36 ≈ 11.1
    LightDesc off;
    off.type = LightDesc::Type::Point;
    off.enabled = false;
    off.intensity = 1e6f;
    LightDesc sun;
    sun.type = LightDesc::Type::Directional;
    sun.azimuthDeg = 30.0f;
    sun.elevationDeg = 50.0f;
    sun.intensity = 0.2f; // en güçlünün %5'inden sönük: gölge almaz
    const auto rt = packRealtimeLights({sun, spot, inner, off}, RealtimeEnvironment{}, AABB(Vec3f(-1), Vec3f(1)));
    ASSERT_EQ(rt.size(), 3u); // kapalı ışık yok
    EXPECT_EQ(rt[0].type, RealtimeLight::Type::Point); // en çok ışınım veren önce
    EXPECT_FALSE(rt[0].castsShadow);
    EXPECT_EQ(rt[1].type, RealtimeLight::Type::Spot);
    EXPECT_TRUE(rt[1].castsShadow);
    EXPECT_NEAR(rt[1].cosOuter, std::cos(20.0f * DEG_TO_RAD), 1e-5f);
    EXPECT_NEAR(rt[1].cosInner, std::cos(10.0f * DEG_TO_RAD), 1e-5f);
    EXPECT_FALSE(rt[2].castsShadow);
    EXPECT_NEAR(rt[2].direction.dot(-sphericalDirection(30.0f, 50.0f)), 1.0f, 1e-5f);
    // Sınır: en fazla maxLights.
    EXPECT_EQ(packRealtimeLights({sun, spot, inner}, RealtimeEnvironment{}, AABB(Vec3f(-1), Vec3f(1)), 2).size(), 2u);
    // Stüdyo: üç softbox'ın üçü de gölge düşürür; maxShadows sınırı en güçlüleri seçer.
    LightDesc key, fill, rim;
    for (LightDesc* d : {&key, &fill, &rim}) {
        d->type = LightDesc::Type::Area;
        d->width = d->height = 1.0f;
    }
    key.position = Vec3f(-3, 4, 4);
    key.intensity = 6.0f;
    fill.position = Vec3f(4, 2, 3);
    fill.intensity = 1.6f;
    rim.position = Vec3f(1, 3, -5);
    rim.intensity = 8.0f;
    const auto studio = packRealtimeLights({key, fill, rim}, RealtimeEnvironment{}, AABB(Vec3f(-1), Vec3f(1)));
    ASSERT_EQ(studio.size(), 3u);
    for (const auto& l : studio) EXPECT_TRUE(l.castsShadow);
    const auto two = packRealtimeLights({key, fill, rim}, RealtimeEnvironment{}, AABB(Vec3f(-1), Vec3f(1)), 16, 2);
    EXPECT_TRUE(two[0].castsShadow);
    EXPECT_TRUE(two[1].castsShadow);
    EXPECT_FALSE(two[2].castsShadow);
    EXPECT_NEAR(two[2].intensity.luminance(), 1.6f, 1e-3f); // en sönük: dolgu
}

TEST(RealtimeShadow, FrustumCoversSceneBox) {
    const AABB box(Vec3f(-2.0f, 0.0f, -1.0f), Vec3f(3.0f, 2.5f, 1.5f));
    RealtimeLight lights[4];
    lights[0].type = RealtimeLight::Type::Directional;
    lights[0].direction = Vec3f(0.3f, -0.8f, 0.2f).normalized();
    lights[1].type = RealtimeLight::Type::Directional;
    lights[1].direction = Vec3f(0.0f, -1.0f, 0.0f); // tam tepeden: up vektörü yedeğe düşer
    lights[2].type = RealtimeLight::Type::Spot;
    lights[2].position = Vec3f(6.0f, 7.0f, 5.0f);
    lights[3].type = RealtimeLight::Type::Area;
    lights[3].position = Vec3f(0.5f, 9.0f, 0.2f);
    for (const RealtimeLight& l : lights) {
        const ShadowProjection sp = shadowProjection(l, box);
        const Mat4f& vp = sp.viewProj;
        EXPECT_EQ(sp.ortho, l.type == RealtimeLight::Type::Directional);
        // Gölge izi de kapsanır: alçak ışıkta zemine düşen gölge kutunun dışına taşar.
        const AABB region = shadowReceiverBounds(l, box);
        EXPECT_LE(region.pMin.x, box.pMin.x);
        EXPECT_GE(region.pMax.x, box.pMax.x);
        for (int c = 0; c < 8; ++c) {
            const Vec3f p((c & 1) ? region.pMax.x : region.pMin.x, (c & 2) ? region.pMax.y : region.pMin.y,
                          (c & 4) ? region.pMax.z : region.pMin.z);
            const Vec4f h = vp * Vec4f(p.x, p.y, p.z, 1.0f);
            ASSERT_GT(h.w, 0.0f);
            for (const float v : {h.x / h.w, h.y / h.w, h.z / h.w}) {
                EXPECT_GE(v, -1.0f);
                EXPECT_LE(v, 1.0f);
            }
        }
    }
}

TEST(RealtimeShadow, LowLightShadowFootprintIsCovered) {
    // 1 m'lik küp, ışık 20° yükseklikten: tepe köşesinin gölgesi 1/tan20° ≈ 2.75 m öteye düşer.
    const AABB box(Vec3f(-0.5f, 0.0f, -0.5f), Vec3f(0.5f, 1.0f, 0.5f));
    RealtimeLight sun;
    sun.type = RealtimeLight::Type::Directional;
    sun.direction = Vec3f(std::cos(20.0f * DEG_TO_RAD), -std::sin(20.0f * DEG_TO_RAD), 0.0f);
    const AABB region = shadowReceiverBounds(sun, box);
    EXPECT_NEAR(region.pMax.x, 0.5f + 1.0f / std::tan(20.0f * DEG_TO_RAD), 1e-3f);
    EXPECT_FLOAT_EQ(region.pMin.x, -0.5f);
    EXPECT_FLOAT_EQ(region.pMin.y, 0.0f);
    // İz zemindedir ve izdüşümün içinde kalır.
    const Vec3f tip(region.pMax.x - 1e-3f, 0.0f, 0.0f);
    const Vec4f h = shadowProjection(sun, box).viewProj * Vec4f(tip.x, tip.y, tip.z, 1.0f);
    EXPECT_LT(std::abs(h.x / h.w), 1.0f);
    EXPECT_LT(std::abs(h.y / h.w), 1.0f);
    // Işık köşenin altındaysa (yukarı doğru) iz yok.
    RealtimeLight up = sun;
    up.direction = Vec3f(0.3f, 0.9f, 0.0f).normalized();
    EXPECT_FLOAT_EQ(shadowReceiverBounds(up, box).pMax.x, 0.5f);
}

TEST(RealtimeToneLut, MatchesViewportToneMapping) {
    RNG rng(7);
    for (const ToneMapOperator op : {ToneMapOperator::AgX, ToneMapOperator::ACES, ToneMapOperator::PBRNeutral,
                                     ToneMapOperator::Filmic, ToneMapOperator::Reinhard}) {
        const ToneLut lut = buildToneLut(op);
        double sumErr = 0.0;
        float maxErr = 0.0f;
        const int N = 4000;
        for (int i = 0; i < N; ++i) {
            const Color3f hdr(std::exp2(rng.uniformFloat() * 16.0f - 10.0f), std::exp2(rng.uniformFloat() * 16.0f - 10.0f),
                              std::exp2(rng.uniformFloat() * 16.0f - 10.0f));
            const Color3f a = sampleToneLut(lut, hdr), b = toneMap(hdr, op, 0.0f);
            for (const float e : {std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b)}) {
                maxErr = std::max(maxErr, e);
                sumErr += e;
            }
        }
        SCOPED_TRACE(toneMapName(op));
        EXPECT_LT(maxErr, 0.02f); // ~5/255; PBR Neutral max/min kırışıklıklarında en büyük
        EXPECT_LT(sumErr / (3.0 * N), 0.004);
    }
}
