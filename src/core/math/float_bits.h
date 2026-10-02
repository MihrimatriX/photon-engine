#pragma once

/// @file float_bits.h
/// @brief NaN/Inf tests that survive fast-math.
///
/// /fp:fast and -ffinite-math-only let the compiler assume NaN and Inf never
/// occur, so std::isnan / std::isfinite may be folded to constants. Testing the
/// exponent bits cannot be folded away.

#include <bit>
#include <cstdint>

namespace photon {

/// True when @p x is neither NaN nor ±Inf.
inline bool finiteFloat(float x) {
    return (std::bit_cast<uint32_t>(x) & 0x7f800000u) != 0x7f800000u;
}

inline bool finite3(float x, float y, float z) {
    return finiteFloat(x) && finiteFloat(y) && finiteFloat(z);
}

} // namespace photon
