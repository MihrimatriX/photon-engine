#include <gtest/gtest.h>
#include "samplers/stratified_sampler.h"
#include "samplers/independent_sampler.h"
#include "samplers/sobol_sampler.h"
#include "engine/renderer.h"
#include <cmath>

using namespace photon;

TEST(StratifiedSampler, PixelSamplesCoverDistinctStrata) {
    StratifiedSampler sampler(4, 4, 7);
    bool cell[4][4] = {};
    for (int i = 0; i < 16; ++i) {
        sampler.startPixel(3, 5);
        sampler.startSample(i);
        Vec2f u = sampler.get2D();
        EXPECT_GE(u.x, 0.0f);
        EXPECT_LT(u.x, 1.0f);
        EXPECT_GE(u.y, 0.0f);
        EXPECT_LT(u.y, 1.0f);
        int cx = std::min(3, static_cast<int>(u.x * 4.0f));
        int cy = std::min(3, static_cast<int>(u.y * 4.0f));
        EXPECT_FALSE(cell[cx][cy]) << "sample " << i << " repeated stratum " << cx << "," << cy;
        cell[cx][cy] = true;

        Vec2f u2 = sampler.get2D();
        EXPECT_GE(u2.x, 0.0f);
        EXPECT_LT(u2.x, 1.0f);
    }
}

TEST(StratifiedSampler, RepeatIsDeterministic) {
    StratifiedSampler a(4, 4, 11);
    StratifiedSampler b(4, 4, 11);
    a.startPixel(1, 2);
    b.startPixel(1, 2);
    a.startSample(3);
    b.startSample(3);
    Vec2f ua = a.get2D();
    Vec2f ub = b.get2D();
    EXPECT_EQ(ua.x, ub.x);
    EXPECT_EQ(ua.y, ub.y);
}

TEST(RenderSampler, ProductionIsSobol) {
    auto sampler = createRenderSampler(16, 12345);
    EXPECT_NE(dynamic_cast<SobolSampler*>(sampler.get()), nullptr);
    EXPECT_EQ(dynamic_cast<IndependentSampler*>(sampler.get()), nullptr);

    IndependentSampler tests(4);
    IndependentSampler again(4);
    EXPECT_EQ(tests.get1D(), again.get1D());
}
