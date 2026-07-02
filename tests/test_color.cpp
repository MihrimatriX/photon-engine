#include "gtest/gtest.h"
#include "core/color/spectrum.h"

using namespace photon;

TEST(ColorTest, Construction) {
    Color3f c1;
    EXPECT_FLOAT_EQ(c1.r, 0.0f);
    EXPECT_FLOAT_EQ(c1.g, 0.0f);
    EXPECT_FLOAT_EQ(c1.b, 0.0f);

    Color3f c2(1.0f, 0.5f, 0.25f);
    EXPECT_FLOAT_EQ(c2.r, 1.0f);
    EXPECT_FLOAT_EQ(c2.g, 0.5f);
    EXPECT_FLOAT_EQ(c2.b, 0.25f);
}

TEST(ColorTest, Arithmetic) {
    Color3f a(1.0f, 0.5f, 0.2f);
    Color3f b(0.5f, 0.5f, 0.8f);

    Color3f sum = a + b;
    EXPECT_FLOAT_EQ(sum.r, 1.5f);
    EXPECT_FLOAT_EQ(sum.g, 1.0f);
    EXPECT_FLOAT_EQ(sum.b, 1.0f);

    Color3f prod = a * b;
    EXPECT_FLOAT_EQ(prod.r, 0.5f);
    EXPECT_FLOAT_EQ(prod.g, 0.25f);
    EXPECT_FLOAT_EQ(prod.b, 0.16f);
}

TEST(ColorTest, Luminance) {
    Color3f white(1.0f);
    EXPECT_NEAR(white.luminance(), 1.0f, 1e-4f);

    Color3f black(0.0f);
    EXPECT_FLOAT_EQ(black.luminance(), 0.0f);
}

TEST(ColorTest, ValidityAndClamping) {
    Color3f c1(1.5f, -0.5f, 0.5f);
    EXPECT_FALSE(c1.isValid()); // Negative value is invalid

    Color3f clamped = c1.clamp(0.0f, 1.0f);
    EXPECT_FLOAT_EQ(clamped.r, 1.0f);
    EXPECT_FLOAT_EQ(clamped.g, 0.0f);
    EXPECT_FLOAT_EQ(clamped.b, 0.5f);
    EXPECT_TRUE(clamped.isValid());
}
