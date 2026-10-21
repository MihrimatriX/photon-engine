// Renk (Color3f) aritmetiği ve sRGB aktarım eğrisi testleri.

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

// sRGB OETF (IEC 61966-2-1): encode, decode'un tam tersi olmalı ve siyaha yakın
// doğrusal ayağı (12.92·x) kullanmalı. Eski gamma 2.2: encode(0.001) = 0.043.
TEST(ColorTest, SrgbEncodeIsExactPiecewiseCurve) {
    EXPECT_NEAR(srgbEncode(0.001f), 0.01292f, 1e-5f);
    EXPECT_NEAR(srgbEncode(0.0f), 0.0f, 1e-7f);
    EXPECT_NEAR(srgbEncode(1.0f), 1.0f, 1e-5f);
    EXPECT_NEAR(srgbEncode(0.18f), 0.46135f, 1e-4f); // orta gri
    for (int i = 0; i <= 1000; ++i) {
        float v = static_cast<float>(i) / 1000.0f;
        EXPECT_NEAR(srgbEncode(srgbDecode(v)), v, 2e-5f) << "v=" << v;
    }
}
