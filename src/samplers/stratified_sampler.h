// stratified_sampler.h — Tabakalı (jittered) örnekleyici: alanı nx × ny hücreye bölüp her
// hücreye bir örnek düşürür. Örnek sayısı nx·ny ile sabittir; sayı önceden bilinmeyen
// ilerlemeli render'da Owen-karıştırmalı Sobol daha uygundur.
#pragma once

/// @file stratified_sampler.h
/// @brief Stratified sampler for jittered pixel and lens sampling in PhotonEngine.

#include "samplers/sampler.h"
#include "core/random/rng.h"

namespace photon {

/// @brief Sampler that divides pixel domain into strata to reduce variance.
class StratifiedSampler : public Sampler {
public:
    StratifiedSampler(int xSamples, int ySamples, uint64_t seed = 0);

    float get1D() override;
    Vec2f get2D() override;
    std::unique_ptr<Sampler> clone(uint64_t seed) const override;
    void startPixel(int x, int y) override;
    void startSample(int sampleIndex) override;

private:
    int m_xSamples;
    int m_ySamples;
    uint64_t m_baseSeed;
    
    RNG m_rng;
    int m_currentSampleIndex = 0;
    int m_pixelX = 0;
    int m_pixelY = 0;
    int m_get1DCallCount = 0;
    int m_get2DCallCount = 0;
};

} // namespace photon
