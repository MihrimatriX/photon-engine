// float_bits.h — Hızlı-matematik (fast-math) derleme bayraklarında bile çalışan NaN/Inf testleri.
// Örnek birikiminde tek bir NaN/Inf tüm pikseli bozar; bu yüzden Film bu testi kullanır.
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
// IEEE-754 float: 1 işaret + 8 üs + 23 kesir biti. Üs bitlerinin hepsi 1 (0x7f800000)
// ise sayı ya ±Inf (kesir = 0) ya da NaN'dır (kesir ≠ 0). Bit maskesi bunu yakalar.
inline bool finiteFloat(float x) {
    return (std::bit_cast<uint32_t>(x) & 0x7f800000u) != 0x7f800000u;
}

inline bool finite3(float x, float y, float z) {
    return finiteFloat(x) && finiteFloat(y) && finiteFloat(z);
}

} // namespace photon
