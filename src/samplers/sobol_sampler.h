// Owen-karıştırmalı Sobol örnekleyici (Burley 2020, "Practical Hash-based Owen
// Scrambling", JCGT 9(4)). Her piksel ve her boyut için bağımsız karıştırılmış,
// ilerlemeli (progressive) render'da her ön-ek (1, 2, 4, ... örnek) iyi tabakalanmış
// düşük-tutarsızlıklı (low-discrepancy) noktalar üretir. Durum yalnızca
// startPixel/startSample ile belirlenir: aynı (piksel, örnek, boyut) hep aynı sayıyı verir.
#pragma once

/// @file sobol_sampler.h
/// @brief Hash-based Owen-scrambled Sobol sampler (Burley 2020).

#include "samplers/sampler.h"
#include <cstdint>
#include <memory>

namespace photon {

/// @brief Owen-scrambled, index-shuffled 2D Sobol sampler with per-dimension padding.
///
/// Every get1D()/get2D() call is a new "dimension". Each dimension uses the first two
/// Sobol dimensions with its own hash-derived scramble, so dimensions are decorrelated
/// and every pixel gets its own randomization. Works for any sample count and any
/// sample index (passes 0, 1, 2, ... in a progressive render). Values are in [0, 1).
///
/// Use the same clone seed for a pixel across passes: the scramble must stay fixed
/// while startSample() walks the sequence, or the stratification is lost.
class SobolSampler : public Sampler {
public:
    explicit SobolSampler(uint64_t seed = 0);

    float get1D() override;
    Vec2f get2D() override;
    std::unique_ptr<Sampler> clone(uint64_t seed) const override;
    void startPixel(int x, int y) override;
    void startSample(int sampleIndex) override;

private:
    uint64_t m_seed;
    uint32_t m_pixelHash = 0;
    uint32_t m_index = 0;
    uint32_t m_dimension = 0;
};

/// Factory for the renderer (kept as a free function so callers need only this header).
std::unique_ptr<Sampler> createSobolSampler(uint64_t seed);

} // namespace photon
