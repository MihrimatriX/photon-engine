// test_environment.cpp — HDRI ortam ışığı testleri: önem örnekleme dağılımının integrali 1,
// düz beyaz haritada pdf = 1/4π, parlak bölgenin sık seçilmesi, döndürmede sample/pdf
// tutarlılığı ve kutuplarda yatay sarma (wrap) olmaması. pdf tutarsızsa ortam ışığı yanlış
// parlaklıkta katkı verir.
#include <gtest/gtest.h>
#include "lights/environment_light.h"
#include "core/sampling/sampling.h"
#include <algorithm>

using namespace photon;

namespace {

Image whiteMap(int w, int h) {
    Image img(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) img.setPixel(x, y, Color3f(1.0f));
    return img;
}

} // namespace

TEST(EnvironmentLight, CdfIntegratesToOne) {
    Image img = whiteMap(32, 16);
    EnvironmentLight env(std::make_shared<const Image>(img), 0.0f, 1.0f);
    EXPECT_NEAR(env.distributionIntegral(), 1.0f, 1e-4f);

    // Constant luminance + sin(theta) weight is uniform over the sphere.
    EXPECT_NEAR(env.pdfLi(Vec3f(1.0f, 0.0f, 0.0f)), uniformSpherePdf(), 0.02f);
    EXPECT_NEAR(env.pdfLi(Vec3f(0.0f, 0.0f, 1.0f)), uniformSpherePdf(), 0.02f);
    EXPECT_NEAR(env.pdfLi(Vec3f(0.0f, 0.0f, -1.0f), Vec3f(0, 1, 0)), uniformSpherePdf(), 0.02f);

    SurfaceInteraction si;
    si.normal = Vec3f(0, 1, 0);
    LightSample ls = env.sampleLi(si, Vec2f(0.37f, 0.62f));
    EXPECT_GT(ls.pdf, 0.0f);
    EXPECT_NEAR(ls.pdf, env.pdfLi(ls.wi), std::max(1e-3f, ls.pdf * 0.15f));
}

TEST(EnvironmentLight, SamplesBrightWindow) {
    constexpr int W = 32, H = 16;
    Image img(W, H);
    for (int y = 4; y < 10; ++y)
        for (int x = 8; x < 24; ++x) img.setPixel(x, y, Color3f(80.0f));

    EnvironmentLight env(std::make_shared<const Image>(img), 0.0f, 1.0f);
    EXPECT_NEAR(env.distributionIntegral(), 1.0f, 1e-4f);

    SurfaceInteraction si;
    si.normal = Vec3f(0, 1, 0);
    constexpr int N = 16;
    int inside = 0;
    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
            LightSample ls = env.sampleLi(si, Vec2f((i + 0.5f) / N, (j + 0.5f) / N));
            Vec2f uv = directionToEquirect(ls.wi);
            int x = std::min(static_cast<int>(uv.x * W), W - 1);
            int y = std::min(static_cast<int>(uv.y * H), H - 1);
            if (x >= 8 && x < 24 && y >= 4 && y < 10) ++inside;
        }
    }
    EXPECT_GT(inside, N * N * 8 / 10);
}

TEST(EnvironmentLight, RotationKeepsPdfConsistent) {
    constexpr int W = 32, H = 16;
    Image img(W, H);
    for (int y = 4; y < 10; ++y)
        for (int x = 8; x < 14; ++x) img.setPixel(x, y, Color3f(50.0f));
    EnvironmentLight env(std::make_shared<const Image>(img), 1.3f, 2.0f);
    SurfaceInteraction si;
    for (int i = 0; i < 16; ++i) {
        LightSample ls = env.sampleLi(si, Vec2f((i + 0.5f) / 16.0f, 0.4f));
        ASSERT_GT(ls.pdf, 0.0f);
        EXPECT_NEAR(ls.pdf, env.pdfLi(ls.wi), std::max(1e-3f, ls.pdf * 0.15f));
        // Sampled directions land on the (rotated) bright patch, scaled by intensity.
        EXPECT_GT(ls.Li.r, 10.0f);
    }
}

TEST(EnvironmentLight, PolesDoNotWrap) {
    // Top row white, bottom row black: looking straight up must not blend in the nadir.
    Image img(8, 8);
    for (int x = 0; x < 8; ++x) img.setPixel(x, 0, Color3f(1.0f));
    EXPECT_NEAR(sampleEquirect(img, 0.3f, 0.0f).r, 1.0f, 1e-5f);
    EXPECT_NEAR(sampleEquirect(img, 0.3f, 1.0f).r, 0.0f, 1e-5f);
}
