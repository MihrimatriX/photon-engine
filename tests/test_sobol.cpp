// SobolSampler testleri: [0,1) aralığı, determinizm, pikseller arası ilintisizlik,
// 2'nin kuvveti ön-eklerde tabakalanma ve bağımsız örnekleyiciye göre daha hızlı yakınsama.

#include <gtest/gtest.h>
#include "samplers/sobol_sampler.h"
#include "samplers/independent_sampler.h"
#include "core/random/rng.h"
#include <cmath>
#include <vector>

using namespace photon;

TEST(SobolSampler, ValuesInHalfOpenUnitInterval) {
    SobolSampler s(123);
    for (int px = 0; px < 16; ++px) {
        s.startPixel(px, 3 * px);
        for (int i = 0; i < 300; ++i) {
            s.startSample(i);
            for (int d = 0; d < 8; ++d) {
                float a = s.get1D();
                Vec2f b = s.get2D();
                ASSERT_GE(a, 0.0f);
                ASSERT_LT(a, 1.0f);
                ASSERT_GE(b.x, 0.0f);
                ASSERT_LT(b.x, 1.0f);
                ASSERT_GE(b.y, 0.0f);
                ASSERT_LT(b.y, 1.0f);
            }
        }
    }
}

// Aynı (piksel, örnek, boyut) her zaman aynı sayı; araya başka pikseller girse bile.
TEST(SobolSampler, DeterministicWithoutCarriedState) {
    SobolSampler a(9);
    auto b = a.clone(9);
    a.startPixel(5, 7);
    a.startSample(1000003);
    float a0 = a.get1D();
    Vec2f a1 = a.get2D();

    b->startPixel(100, 200);
    b->startSample(3);
    (void)b->get2D();
    b->startPixel(5, 7);
    b->startSample(1000003);
    EXPECT_EQ(b->get1D(), a0);
    Vec2f b1 = b->get2D();
    EXPECT_EQ(b1.x, a1.x);
    EXPECT_EQ(b1.y, a1.y);

    auto other = createSobolSampler(10);
    other->startPixel(5, 7);
    other->startSample(1000003);
    EXPECT_NE(other->get1D(), a0);
}

// Her piksel kendi karıştırmasını alır: 4096 pikselde örnek 0'ın ortalaması ≈ 0.5
// ve varyansı ≈ 1/12. (StratifiedSampler'da tüm pikseller aynı tabakayı seçiyordu.)
TEST(SobolSampler, PixelsAreDecorrelated) {
    SobolSampler s(77);
    for (int dim = 0; dim < 3; ++dim) {
        double sum = 0.0;
        double sum2 = 0.0;
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                s.startPixel(x, y);
                s.startSample(0);
                for (int d = 0; d < dim; ++d) (void)s.get2D();
                float v = s.get2D().y;
                sum += v;
                sum2 += static_cast<double>(v) * v;
            }
        }
        double mean = sum / 4096.0;
        double var = sum2 / 4096.0 - mean * mean;
        EXPECT_NEAR(mean, 0.5, 0.02) << "dim " << dim;
        EXPECT_NEAR(var, 1.0 / 12.0, 0.01) << "dim " << dim;
    }
}

// İlk 16 örnek her 2B boyutta 4x4 ızgaranın her hücresine tam bir kez düşmeli
// ((0,2)-dizisi + Owen karıştırması ağ özelliğini korur; indeks karıştırma hizalı
// blokları korur). Aynı şey 16..31 örnekleri için de geçerli.
TEST(SobolSampler, PowerOfTwoPrefixesAreStratified) {
    SobolSampler s(5);
    for (int start : {0, 16}) {
        for (int dim = 0; dim < 4; ++dim) {
            bool cell[4][4] = {};
            for (int i = start; i < start + 16; ++i) {
                s.startPixel(11, 4);
                s.startSample(i);
                for (int d = 0; d < dim; ++d) (void)s.get2D();
                Vec2f u = s.get2D();
                int cx = static_cast<int>(u.x * 4.0f);
                int cy = static_cast<int>(u.y * 4.0f);
                EXPECT_FALSE(cell[cx][cy]) << "dim " << dim << " sample " << i;
                cell[cx][cy] = true;
            }
        }
    }
}

// 64 spp ile ∫∫ exp(-(u-0.3)² - (v-0.6)²) du dv tahmini; 256 piksel = 256 bağımsız deneme.
// Kesin değer ayrılabilir: ∫₀¹ exp(-(u-a)²) du = (√π/2)(erf(1-a) + erf(a)).
// Sobol'ün RMS hatası bağımsız örnekleyicininkinden belirgin küçük olmalı.
TEST(SobolSampler, ConvergesFasterThanIndependent) {
    auto f = [](Vec2f u) {
        float a = u.x - 0.3f;
        float b = u.y - 0.6f;
        return std::exp(-(a * a + b * b));
    };
    auto g = [](double a) { return 0.5 * std::sqrt(3.14159265358979) * (std::erf(1.0 - a) + std::erf(a)); };
    const double exact = g(0.3) * g(0.6);

    auto rmse = [&](Sampler& sampler) {
        double err2 = 0.0;
        for (int p = 0; p < 256; ++p) {
            sampler.startPixel(p % 16, p / 16);
            double est = 0.0;
            for (int i = 0; i < 64; ++i) {
                sampler.startSample(i);
                (void)sampler.get2D(); // integrali 2. boyutta yap (dolgu da test edilsin)
                est += f(sampler.get2D());
            }
            est /= 64.0;
            err2 += (est - exact) * (est - exact);
        }
        return std::sqrt(err2 / 256.0);
    };

    SobolSampler sobol(2024);
    IndependentSampler indep(2024);
    double eSobol = rmse(sobol);
    double eIndep = rmse(indep);
    EXPECT_LT(eSobol, 0.25 * eIndep) << "sobol " << eSobol << " independent " << eIndep;
}
