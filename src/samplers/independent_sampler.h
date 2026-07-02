#pragma once

/// @file independent_sampler.h
/// @brief Independent random number sampler in PhotonEngine.

#include "samplers/sampler.h"
#include "core/random/rng.h"

namespace photon {

/// @brief Sampler that produces independent pseudo-random numbers (white noise).
class IndependentSampler : public Sampler {
public:
    explicit IndependentSampler(uint64_t seed = 0);

    float get1D() override;
    Vec2f get2D() override;
    std::unique_ptr<Sampler> clone(uint64_t seed) const override;
    void startPixel(int x, int y) override;
    void startSample(int sampleIndex) override;

private:
    RNG m_rng;
    uint64_t m_baseSeed;
};

} // namespace photon
