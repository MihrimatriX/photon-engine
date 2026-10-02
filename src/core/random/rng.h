#pragma once

#include <cstdint>
#include "core/math/vec.h"

namespace photon {

class RNG {
public:
    explicit RNG(uint64_t seed = 0x853c49e6748fea9bULL) {
        m_state = 0;
        m_inc = (seed << 1u) | 1u;
        uniformUint32();
        m_state += 0x853c49e6748fea9bULL;
        uniformUint32();
    }

    uint32_t uniformUint32() {
        uint64_t oldState = m_state;
        m_state = oldState * 6364136223846793005ULL + m_inc;
        uint32_t xorShifted = static_cast<uint32_t>(((oldState >> 18u) ^ oldState) >> 27u);
        uint32_t rot = static_cast<uint32_t>(oldState >> 59u);
        return (xorShifted >> rot) | (xorShifted << ((0u - rot) & 31));
    }

    float uniformFloat() {
        return static_cast<float>(uniformUint32()) / 4294967296.0f;  // divide by 2^32
    }

    Vec2f uniformFloat2D() {
        return {uniformFloat(), uniformFloat()};
    }

private:
    uint64_t m_state;
    uint64_t m_inc;
};

} // namespace photon
