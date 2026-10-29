// Piksel filtresi testleri: kutu filtrenin eski titremeyle birebir aynı olması, örneklenen
// ofsetlerin filtre şeklini izlemesi (Kolmogorov–Smirnov) ve ad ↔ tür dönüşümü.

#include <gtest/gtest.h>
#include "core/image/pixel_filter.h"
#include "samplers/independent_sampler.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace photon;

TEST(PixelFilter, BoxReproducesPlainJitter) {
    const PixelFilterSampler box(PixelFilter{PixelFilterType::Box, 1.0f});
    for (float u : {0.0f, 0.1f, 0.5f, 0.93f}) EXPECT_FLOAT_EQ(box.sample1D(u), u - 0.5f);
}

TEST(PixelFilter, SamplesFollowFilterShape) {
    for (PixelFilterType t : {PixelFilterType::Triangle, PixelFilterType::Gaussian, PixelFilterType::BlackmanHarris}) {
        for (float width : {1.5f, 2.0f, 3.0f}) {
            const PixelFilterSampler f(PixelFilter{t, width});
            // Örnekler: ters CDF ile.
            IndependentSampler rng(3);
            constexpr int n = 20000;
            std::vector<float> xs(n);
            for (float& x : xs) x = f.sample1D(rng.get1D());
            std::sort(xs.begin(), xs.end());
            EXPECT_GE(xs.front(), -f.radius());
            EXPECT_LE(xs.back(), f.radius());
            // Beklenen CDF: filtrenin sayısal integrali.
            constexpr int m = 4000;
            std::vector<double> cdf(m + 1, 0.0);
            const double dx = 2.0 * f.radius() / m;
            for (int i = 0; i < m; ++i)
                cdf[static_cast<size_t>(i) + 1] = cdf[static_cast<size_t>(i)] +
                                                  f.evaluate(static_cast<float>(-f.radius() + (i + 0.5) * dx)) * dx;
            for (double& c : cdf) c /= cdf.back();
            double ks = 0.0;
            for (int i = 0; i < n; ++i) {
                const double pos = (xs[static_cast<size_t>(i)] + f.radius()) / dx;
                const int k = std::clamp(static_cast<int>(pos), 0, m - 1);
                const double frac = pos - k;
                const double expected = cdf[static_cast<size_t>(k)] * (1.0 - frac) + cdf[static_cast<size_t>(k) + 1] * frac;
                ks = std::max({ks, std::abs(expected - static_cast<double>(i) / n),
                               std::abs(expected - static_cast<double>(i + 1) / n)});
            }
            // KS kritik değeri (α = 0.001): 1.95 / √n ≈ 0.0138.
            EXPECT_LT(ks, 1.95 / std::sqrt(static_cast<double>(n)))
                << pixelFilterName(t) << " width " << width;
            // Simetri: ortalama ofset ≈ 0 (görüntü kaymaz).
            double mean = 0.0;
            for (float x : xs) mean += x;
            EXPECT_NEAR(mean / n, 0.0, 0.02) << pixelFilterName(t);
        }
    }
}

TEST(PixelFilter, NamesRoundTrip) {
    for (PixelFilterType t : {PixelFilterType::Box, PixelFilterType::Triangle, PixelFilterType::Gaussian,
                              PixelFilterType::BlackmanHarris}) {
        PixelFilterType back = PixelFilterType::Box;
        ASSERT_TRUE(parsePixelFilter(pixelFilterName(t), back));
        EXPECT_EQ(back, t);
    }
    PixelFilterType x;
    EXPECT_FALSE(parsePixelFilter("lanczos", x));
}
