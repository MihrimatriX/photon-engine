#include "gtest/gtest.h"
#include "core/sampling/sampling.h"

using namespace photon;

TEST(SamplingTest, HemisphereSampling) {
    Vec2f u(0.35f, 0.72f);
    
    // Uniform hemisphere
    Vec3f v1 = uniformSampleHemisphere(u);
    EXPECT_TRUE(v1.z >= 0.0f);
    EXPECT_NEAR(v1.length(), 1.0f, 1e-5f);

    // Cosine-weighted hemisphere
    Vec3f v2 = cosineSampleHemisphere(u);
    EXPECT_TRUE(v2.z >= 0.0f);
    EXPECT_NEAR(v2.length(), 1.0f, 1e-5f);
}

TEST(SamplingTest, DiskSampling) {
    Vec2f u(0.45f, 0.82f);
    Vec2f d = uniformSampleDisk(u);
    
    // Point must be inside unit disk
    float r2 = d.x * d.x + d.y * d.y;
    EXPECT_TRUE(r2 <= 1.0f);
}
