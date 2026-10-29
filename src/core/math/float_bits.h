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

/// Bir sonraki büyük temsil edilebilir float (PBRT-v4 NextFloatUp). +∞ ve NaN olduğu gibi döner.
/// IEEE-754'te pozitif sayıların bit deseni sayıyla birlikte artar; negatiflerde azalır.
inline float nextFloatUp(float v) {
    if (!finiteFloat(v) && v > 0.0f) return v;
    if (v == -0.0f) v = 0.0f; // −0 ve +0 aynı sayı; +0'dan yukarı
    uint32_t ui = std::bit_cast<uint32_t>(v);
    if (v >= 0.0f) ++ui;
    else --ui;
    return std::bit_cast<float>(ui);
}

/// Bir sonraki küçük temsil edilebilir float.
inline float nextFloatDown(float v) {
    if (!finiteFloat(v) && v < 0.0f) return v;
    if (v == 0.0f) v = -0.0f;
    uint32_t ui = std::bit_cast<uint32_t>(v);
    if (v > 0.0f) --ui;
    else ++ui;
    return std::bit_cast<float>(ui);
}

/// Kayan nokta hata sınırı γ(n) = nε / (1 − nε), ε = 2⁻²⁴ (Higham; PBRT-v4 §6.8).
/// n işlemli bir ifadenin göreli hatası en fazla γ(n)'dir.
constexpr float floatGamma(int n) {
    constexpr float eps = 5.9604645e-8f; // 2^-24
    return (static_cast<float>(n) * eps) / (1.0f - static_cast<float>(n) * eps);
}

} // namespace photon
