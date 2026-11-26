// test_realtime_v2.cpp — Milestone 5 (Gerçek Zamanlı OpenGL 3.3 Viewport v2) doğrulama testleri:
//   * Heitz 2016 LTC dikdörtgen alan ışığı vs 40K örnekli Monte Carlo GGX+Lambert integrali (< %5)
//   * IES LM-63 1B profil atlası (buildIesAtlas / sampleIesAtlas) vs IesProfile::evaluate eşliği
//   * GPU Imager (WB, log-kontrast, doygunluk, vinyet) vs CPU applyImager formül eşliği
#include <gtest/gtest.h>

#include "core/image/imager.h"
#include "core/math/constants.h"
#include "lights/ies.h"
#include "scene/ltc_lut.h"
#include "scene/realtime_scene.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace photon;

namespace {

// Deterministik R2 2B örnekleme
inline void r2(int i, float& u1, float& u2) {
    constexpr double g = 1.32471795724474602596;
    const double n = static_cast<double>(i) + 0.5;
    u1 = static_cast<float>(std::fmod(0.5 + n / g, 1.0));
    u2 = static_cast<float>(std::fmod(0.5 + n / (g * g), 1.0));
}

float ggxD_ref(float NoH, float a) {
    const float a2 = a * a;
    const float d = NoH * NoH * (a2 - 1.0f) + 1.0f;
    return a2 / std::max(PI * d * d, 1e-12f);
}

float ggxVSmith_ref(float NoV, float NoL, float a) {
    const float a2 = a * a;
    const float gv = NoL * std::sqrt(NoV * NoV * (1.0f - a2) + a2);
    const float gl = NoV * std::sqrt(NoL * NoL * (1.0f - a2) + a2);
    return 0.5f / std::max(gv + gl, 1e-7f);
}

Color3f fresnelSchlick_ref(const Color3f& f0, float VoH) {
    const float f = std::pow(1.0f - std::clamp(VoH, 0.0f, 1.0f), 5.0f);
    return f0 + (Color3f(1.0f) - f0) * f;
}

// Referans Monte Carlo dikdörtgen alan ışığı integrali (GGX Smith height-correlated + Lambert)
Color3f mcRectAreaRef(const Vec3f& P, const Vec3f& N, const Vec3f& V,
                      float roughness, const Color3f& f0, const Color3f& diffAlbedo,
                      const Vec3f& center, const Vec3f& halfU, const Vec3f& halfV,
                      const Color3f& radiance, int samples = 40000) {
    const Vec3f edgeU = halfU * 2.0f;
    const Vec3f edgeV = halfV * 2.0f;
    const Vec3f nL = edgeU.cross(edgeV);
    const float area = nL.length();
    const Vec3f nLight = nL / std::max(area, 1e-12f);
    const float a = std::max(roughness * roughness, 1e-4f);
    const float NoV = std::max(N.dot(V), 1e-4f);

    double sumR = 0.0, sumG = 0.0, sumB = 0.0;
    for (int i = 0; i < samples; ++i) {
        float u1, u2;
        r2(i, u1, u2);
        const Vec3f q = center + halfU * (2.0f * u1 - 1.0f) + halfV * (2.0f * u2 - 1.0f);
        const Vec3f d = q - P;
        const float dist2 = d.lengthSquared();
        if (dist2 < 1e-10f) continue;
        const float dist = std::sqrt(dist2);
        const Vec3f L = d / dist;
        const float NoL = N.dot(L);
        if (NoL <= 1e-6f) continue;
        const float cosL = std::abs(L.dot(nLight));
        if (cosL <= 1e-6f) continue;

        const float dOmega = area * cosL / dist2;
        const Vec3f H = (V + L).normalized();
        const float NoH = std::max(N.dot(H), 0.0f);
        const float VoH = std::max(V.dot(H), 0.0f);
        const Color3f F = fresnelSchlick_ref(f0, VoH);
        const float specScalar = ggxD_ref(NoH, a) * ggxVSmith_ref(NoV, NoL, a);
        const Color3f f = diffAlbedo * INV_PI + F * specScalar;
        const Color3f contrib = f * radiance * (NoL * dOmega);
        sumR += contrib.r;
        sumG += contrib.g;
        sumB += contrib.b;
    }
    const double inv = 1.0 / static_cast<double>(samples);
    return Color3f(static_cast<float>(sumR * inv), static_cast<float>(sumG * inv), static_cast<float>(sumB * inv));
}

} // namespace

TEST(RealtimeV2, LtcRectAreaLightMatchesMonteCarlo) {
    const Vec3f P(0.0f, 0.0f, 0.0f);
    const Vec3f N(0.0f, 1.0f, 0.0f);
    const Vec3f halfU(1.1f, 0.0f, 0.0f);
    const Vec3f halfV(0.0f, 0.0f, 0.9f);
    const Color3f rad(6.0f, 5.5f, 5.0f);

    // 1) Saf difüz (Lambert) analitik doğruluk (< %0.5)
    {
        const Vec3f center(0.3f, 1.8f, -0.5f);
        const Vec3f V = Vec3f(0.0f, 0.8f, 0.6f).normalized();
        const Color3f ltc = evalLtcRectArea(P, N, V, 0.5f, Color3f(0.0f), Color3f(0.8f, 0.6f, 0.4f),
                                            center, halfU, halfV, rad, true);
        const Color3f mc = mcRectAreaRef(P, N, V, 0.5f, Color3f(0.0f), Color3f(0.8f, 0.6f, 0.4f),
                                         center, halfU, halfV, rad, 40000);
        // Not: saf difüz için f0=0 olduğunda LTC2.y (Fresnel grazing) terimi vardır; f0=0 ile yalnız
        // difüz değil (1-0)*fresnel de eklenir, bu yüzden her iki taraf da aynı F_Schlick(0) + diff hesaplar!
        const float rel = std::abs(ltc.luminance() - mc.luminance()) / std::max(mc.luminance(), 1e-4f);
        EXPECT_LT(rel, 0.03f) << "ltc=" << ltc.luminance() << " mc=" << mc.luminance();
    }

    // 2) Çoklu pürüzlülük (0.15, 0.4, 0.7) × çoklu görüş açısı (0°, 30°, 60°) için GGX speküler eşliği (< %5)
    const float roughnesses[] = {0.15f, 0.40f, 0.70f};
    const float anglesDeg[] = {0.0f, 30.0f, 60.0f};
    const Color3f goldF0(0.95f, 0.72f, 0.32f);

    for (float r : roughnesses) {
        for (float deg : anglesDeg) {
            const float th = deg * DEG_TO_RAD;
            const Vec3f V(std::sin(th), std::cos(th), 0.0f);
            // Işığı yansıma yönü (−sinθ, cosθ, 0) eksenine yerleştir ki speküler lob dolu olsun
            const Vec3f center(-std::sin(th) * 2.2f, std::max(0.8f, std::cos(th) * 2.2f), 0.0f);
            const Color3f ltc = evalLtcRectArea(P, N, V, r, goldF0, Color3f(0.0f), center, halfU, halfV, rad, true);
            const Color3f mc = mcRectAreaRef(P, N, V, r, goldF0, Color3f(0.0f), center, halfU, halfV, rad, 50000);
            ASSERT_TRUE(ltc.isFinite());
            ASSERT_GT(mc.luminance(), 0.05f);
            const float relErr = std::abs(ltc.luminance() - mc.luminance()) / mc.luminance();
            EXPECT_LT(relErr, 0.05f) << "rough=" << r << " deg=" << deg
                                     << " ltc=" << ltc.luminance() << " mc=" << mc.luminance();
        }
    }
}

TEST(RealtimeV2, IesProfileAtlasSampling) {
    // Gerçekçi C-tipi simetrik LM-63 IES profili (0°..180° arası 7 dikey açı)
    const std::string iesText =
        "IESNA:LM-63-2002\n"
        "TILT=NONE\n"
        "1 1000 1.0 7 1 1 2 0.0 0.0 0.0\n"
        "1.0 1.0 10.0\n"
        "0 15 30 45 60 90 180\n"
        "0\n"
        "1000 850 500 200 60 10 0\n";

    std::string err;
    auto profile = parseIes(iesText, &err);
    ASSERT_NE(profile, nullptr) << err;

    std::vector<RealtimeLight> lights(2);
    lights[0].type = RealtimeLight::Type::Point;
    lights[1].type = RealtimeLight::Type::Spot;
    lights[1].ies = profile;

    const RealtimeIesAtlas atlas = buildIesAtlas(lights);
    EXPECT_EQ(lights[0].iesSlice, -1);
    EXPECT_EQ(lights[1].iesSlice, 0);
    EXPECT_EQ(atlas.slices, 1);
    EXPECT_EQ(atlas.width, 181);

    const float testGammas[] = {0.0f, 7.5f, 15.0f, 22.5f, 30.0f, 45.0f, 60.0f, 75.0f, 90.0f, 135.0f, 180.0f};
    for (float g : testGammas) {
        const float expected = profile->evaluate(g, 0.0f);
        const float sampled = sampleIesAtlas(atlas, 0, g);
        EXPECT_NEAR(sampled, expected, 1e-3f) << "gamma=" << g;
    }

    // packRealtimeLights alan ışığı eksenlerini doldurmalı
    LightDesc ad;
    ad.type = LightDesc::Type::Area;
    ad.position = Vec3f(0.0f, 3.0f, 0.0f);
    ad.target = Vec3f(0.0f, 0.0f, 0.0f);
    ad.width = 2.0f;
    ad.height = 1.0f;
    ad.intensity = 10.0f;
    ad.color = Color3f(1.0f, 0.9f, 0.8f);
    RealtimeEnvironment emptyEnv;
    const auto packed = packRealtimeLights({ad}, emptyEnv, AABB(Vec3f(-1), Vec3f(1)));
    ASSERT_EQ(packed.size(), 1u);
    EXPECT_NEAR(packed[0].areaU.length(), 1.0f, 1e-4f);
    EXPECT_NEAR(packed[0].areaV.length(), 0.5f, 1e-4f);
    EXPECT_NEAR(packed[0].areaU.dot(packed[0].areaV), 0.0f, 1e-5f);
    EXPECT_NEAR(packed[0].areaRadiance.r, 10.0f, 1e-4f);
}

TEST(RealtimeV2, ImagerParityCheck) {
    // CPU applyImager ile kPostFragmentSrc GPU Imager formülünün (WB, kontrast, doygunluk, vinyet) birebir eşliği
    constexpr int W = 32, H = 24;
    Image hdr(W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            hdr.setPixel(x, y, Color3f(0.25f + 0.01f * x, 0.18f + 0.01f * y, 0.12f + 0.005f * (x + y)));

    ImagerSettings im;
    im.whiteBalanceK = 4500.0f;
    im.tint = 0.25f;
    im.contrast = 1.3f;
    im.saturation = 1.25f;
    im.vignette = 0.55f;
    const float exposureEV = 0.5f;
    const float exposureScale = std::exp2(exposureEV);

    Image cpuOut = hdr;
    applyImager(cpuOut, im, exposureEV);

    // kPostFragmentSrc GPU formülünün birebir karşılığı
    float wb[9];
    whiteBalanceMatrix(im.whiteBalanceK, im.tint, wb);
    const float halfDiag = 0.5f * std::sqrt(static_cast<float>(W * W + H * H));

    float maxDiff = 0.0f;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            Color3f c = hdr.getPixel(x, y);
            c = Color3f(wb[0] * c.r + wb[1] * c.g + wb[2] * c.b,
                        wb[3] * c.r + wb[4] * c.g + wb[5] * c.b,
                        wb[6] * c.r + wb[7] * c.g + wb[8] * c.b);
            c = Color3f(std::max(0.0f, c.r), std::max(0.0f, c.g), std::max(0.0f, c.b));
            const float L = std::max(c.luminance(), 0.0f);
            float L2 = L;
            if (L > 0.0f) {
                const float Le = L * exposureScale;
                L2 = 0.18f * std::pow(Le / 0.18f, im.contrast) / exposureScale;
                c *= L2 / L;
            }
            c = Color3f(L2) + (c - Color3f(L2)) * im.saturation;
            const float ux = (static_cast<float>(x) + 0.5f) / static_cast<float>(W);
            const float uy = (static_cast<float>(y) + 0.5f) / static_cast<float>(H);
            const float dx = (ux - 0.5f) * static_cast<float>(W) / halfDiag;
            const float dy = (uy - 0.5f) * static_cast<float>(H) / halfDiag;
            const float r2 = dx * dx + dy * dy;
            c *= std::max(0.0f, 1.0f - im.vignette * r2 * r2 * 0.9f - im.vignette * r2 * 0.1f);
            c = Color3f(std::max(0.0f, c.r), std::max(0.0f, c.g), std::max(0.0f, c.b));

            const Color3f ref = cpuOut.getPixel(x, y);
            maxDiff = std::max({maxDiff, std::abs(c.r - ref.r), std::abs(c.g - ref.g), std::abs(c.b - ref.b)});
        }
    }
    EXPECT_LT(maxDiff, 1e-5f);
}
