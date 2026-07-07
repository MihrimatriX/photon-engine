#pragma once

/// @file sampler.h
/// @brief Abstract Sampler base class in PhotonEngine.

#include "core/math/vec.h"
#include <memory>
#include <cstdint>

namespace photon {

/// @brief Abstract class for generating 1D and 2D sample patterns.
class Sampler {
public:
    virtual ~Sampler() = default;

    /// Get next 1D sample in [0, 1)
    virtual float get1D() = 0;

    /// Get next 2D sample in [0, 1)²
    virtual Vec2f get2D() = 0;

    /// Create an independent clone of the sampler with a new seed
    virtual std::unique_ptr<Sampler> clone(uint64_t seed) const = 0;

    /// Prepare sampler for a new pixel
    virtual void startPixel(int x, int y) = 0;

    /// Prepare sampler for a new sample index in the current pixel
    virtual void startSample(int sampleIndex) = 0;
};

} // namespace photon
