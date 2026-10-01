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
    EnvironmentLight env(&img, 0.0f, 1.0f);
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

    EnvironmentLight env(&img, 0.0f, 1.0f);
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
