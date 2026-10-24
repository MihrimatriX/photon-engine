// Görüntüleyici ve kamera testleri: etkisiz ayarlar görüntüyü değiştirmez, beyaz dengesi
// T sıcaklığındaki beyazı nötrler, bloom yalnız parlak noktanın çevresine ışık ekler,
// vinyet köşeleri karartır; çokgen diyafram örnekleri çokgenin içinde ve düzgün dağılır;
// uyarlamalı örneklemenin göreli hata ölçüsü.

#include <gtest/gtest.h>
#include "camera/thin_lens_camera.h"
#include "core/image/film.h"
#include "core/image/imager.h"
#include "core/math/constants.h"
#include "samplers/independent_sampler.h"

#include <cmath>

using namespace photon;

TEST(Imager, IdentityLeavesImageUntouched) {
    Image img(16, 8);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 16; ++x) img.setPixel(x, y, Color3f(x * 0.1f, y * 0.2f, 0.3f));
    Image copy = img;
    ImagerSettings s;
    EXPECT_TRUE(s.isIdentity());
    applyImager(copy, s);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 16; ++x) EXPECT_EQ(copy.getPixel(x, y), img.getPixel(x, y));
}

TEST(Imager, WhiteBalanceNeutralizesIlluminant) {
    float m[9];
    whiteBalanceMatrix(6504.0f, 0.0f, m); // gün ışığı eğrisinde D65
    for (int i = 0; i < 9; ++i) EXPECT_NEAR(m[i], (i % 4 == 0) ? 1.0f : 0.0f, 2e-3f);
    // Tungsten ışığında beyaz bir kağıt turuncu görünür; 3000 K dengesi onu nötre çeker.
    whiteBalanceMatrix(3000.0f, 0.0f, m);
    // 3000 K Planck beyazı: tablolardan x = 0.4369, y = 0.4041 → XYZ → doğrusal sRGB.
    const float x = 0.4369f, y = 0.4041f;
    const float X = x / y, Y = 1.0f, Z = (1.0f - x - y) / y;
    const Color3f warm(3.2404542f * X - 1.5371385f * Y - 0.4985314f * Z,
                       -0.9692660f * X + 1.8760108f * Y + 0.0415560f * Z,
                       0.0556434f * X - 0.2040259f * Y + 1.0572252f * Z);
    ASSERT_GT(warm.r, 2.0f * warm.b); // gerçekten sıcak
    const Color3f n(m[0] * warm.r + m[1] * warm.g + m[2] * warm.b, m[3] * warm.r + m[4] * warm.g + m[5] * warm.b,
                    m[6] * warm.r + m[7] * warm.g + m[8] * warm.b);
    EXPECT_NEAR(n.r / n.g, 1.0f, 0.06f);
    EXPECT_NEAR(n.b / n.g, 1.0f, 0.06f);
}

TEST(Imager, BloomAddsGlowOnlyAroundBrightPixels) {
    Image img(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) img.setPixel(x, y, Color3f(0.2f));
    for (int y = 30; y < 34; ++y)
        for (int x = 30; x < 34; ++x) img.setPixel(x, y, Color3f(50.0f));
    ImagerSettings s;
    s.bloomIntensity = 0.5f;
    s.bloomRadius = 0.05f;
    applyImager(img, s);
    EXPECT_GT(img.getPixel(36, 32).r, 0.25f); // halenin içinde
    EXPECT_NEAR(img.getPixel(2, 2).r, 0.2f, 0.01f); // uzak köşe değişmez
}

TEST(Imager, VignetteDarkensCornersNotCenter) {
    Image img(40, 30);
    for (int y = 0; y < 30; ++y)
        for (int x = 0; x < 40; ++x) img.setPixel(x, y, Color3f(1.0f));
    ImagerSettings s;
    s.vignette = 0.8f;
    applyImager(img, s);
    EXPECT_GT(img.getPixel(20, 15).r, 0.98f);
    EXPECT_LT(img.getPixel(0, 0).r, 0.5f);
}

TEST(Camera, PolygonalApertureStaysInsidePolygon) {
    // 6 kanatlı diyafram: odak düzlemindeki tek noktaya giden ışınların mercek noktaları.
    ThinLensCamera cam(Vec3f(0, 0, 5), Vec3f(0, 0, 0), Vec3f(0, 1, 0), 40.0f, 1.0f, 1.0f, 5.0f);
    cam.setApertureShape(6, 0.0f);
    IndependentSampler rng(8);
    int inner = 0;
    const int n = 20000;
    for (int i = 0; i < n; ++i) {
        const Ray r = cam.generateRay(0.5f, 0.5f, rng.get2D());
        const float x = r.origin.x, y = r.origin.y;
        const float rad = std::sqrt(x * x + y * y);
        EXPECT_LE(rad, 1.0f + 1e-4f);
        // Altıgenin iç teğet çemberi cos(30°) = 0.866: dışında yalnız köşe dilimleri kalır.
        if (rad < 0.5f) ++inner;
        // Kenar dışı kontrol: her kenarın dış normaline izdüşüm ≤ cos(30°).
        for (int k = 0; k < 6; ++k) {
            const float a = (k + 0.5f) * PI / 3.0f;
            EXPECT_LE(x * std::cos(a) + y * std::sin(a), 0.8661f);
        }
    }
    // Alan-düzgün: r < 0.5 dairesinin alanı / altıgen alanı = (π/4) / (3√3/2) ≈ 0.302.
    EXPECT_NEAR(static_cast<float>(inner) / n, 0.302f, 0.015f);
}

TEST(Film, RelativeErrorShrinksWithSamples) {
    Film f(1, 1);
    IndependentSampler rng(1);
    float e16 = 0, e256 = 0;
    for (int i = 1; i <= 256; ++i) {
        f.addSample(0, 0, Color3f(rng.get1D() * 2.0f));
        if (i == 16) e16 = f.relativeError(0, 0);
    }
    e256 = f.relativeError(0, 0);
    EXPECT_GT(e16, 0.0f);
    EXPECT_NEAR(e256 / e16, 0.25f, 0.08f); // 1/√N
    Film flat(1, 1);
    for (int i = 0; i < 8; ++i) flat.addSample(0, 0, Color3f(0.5f));
    EXPECT_NEAR(flat.relativeError(0, 0), 0.0f, 1e-3f);
}
