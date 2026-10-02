#pragma once

/// @file spectrum.h
/// @brief RGB color spectrum class (Color3f) for the PhotonEngine.

#include "core/color/transfer.h"
#include "core/math/float_bits.h"

#include <cmath>
#include <algorithm>
#include <ostream>
#include <cassert>

namespace photon {

/// @brief Represents a color as an RGB triplet, used as spectrum throughout the engine.
struct Color3f {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;

    // ── Constructors ─────────────────────────────────────────
    constexpr Color3f() = default;
    constexpr explicit Color3f(float v) : r(v), g(v), b(v) {}
    constexpr Color3f(float r, float g, float b) : r(r), g(g), b(b) {}

    // ── Element access ───────────────────────────────────────
    constexpr float  operator[](int i) const { return (&r)[i]; }
    constexpr float& operator[](int i)       { return (&r)[i]; }

    // ── Arithmetic (color ↔ color) ───────────────────────────
    constexpr Color3f operator+(const Color3f& c) const { return {r + c.r, g + c.g, b + c.b}; }
    constexpr Color3f operator-(const Color3f& c) const { return {r - c.r, g - c.g, b - c.b}; }
    constexpr Color3f operator*(const Color3f& c) const { return {r * c.r, g * c.g, b * c.b}; }
    constexpr Color3f operator/(const Color3f& c) const { return {r / c.r, g / c.g, b / c.b}; }

    // ── Arithmetic (color ↔ scalar) ──────────────────────────
    constexpr Color3f operator*(float s) const { return {r * s, g * s, b * s}; }
    constexpr Color3f operator/(float s) const { float inv = 1.0f / s; return {r * inv, g * inv, b * inv}; }

    // ── Compound assignment ──────────────────────────────────
    constexpr Color3f& operator+=(const Color3f& c) { r += c.r; g += c.g; b += c.b; return *this; }
    constexpr Color3f& operator-=(const Color3f& c) { r -= c.r; g -= c.g; b -= c.b; return *this; }
    constexpr Color3f& operator*=(const Color3f& c) { r *= c.r; g *= c.g; b *= c.b; return *this; }
    constexpr Color3f& operator/=(const Color3f& c) { r /= c.r; g /= c.g; b /= c.b; return *this; }
    constexpr Color3f& operator*=(float s)          { r *= s; g *= s; b *= s; return *this; }
    constexpr Color3f& operator/=(float s)          { float inv = 1.0f / s; r *= inv; g *= inv; b *= inv; return *this; }

    // ── Comparison ───────────────────────────────────────────
    constexpr bool operator==(const Color3f& c) const { return r == c.r && g == c.g && b == c.b; }
    constexpr bool operator!=(const Color3f& c) const { return !(*this == c); }

    // ── Utilities ────────────────────────────────────────────
    constexpr Color3f clamp(float low = 0.0f, float high = 1.0f) const {
        return {
            std::max(low, std::min(r, high)),
            std::max(low, std::min(g, high)),
            std::max(low, std::min(b, high))
        };
    }

    /// Calculate perceived luminance using ITU-R BT.709 coefficients
    constexpr float luminance() const {
        return 0.212671f * r + 0.715160f * g + 0.072169f * b;
    }

    constexpr bool isBlack() const {
        return r == 0.0f && g == 0.0f && b == 0.0f;
    }

    /// No NaN or Inf in any channel (bit test, safe under fast-math).
    inline bool isFinite() const {
        return finite3(r, g, b);
    }

    /// Finite and non-negative: a radiance value the film will accept.
    inline bool isValid() const {
        return isFinite() && r >= 0.0f && g >= 0.0f && b >= 0.0f;
    }

    inline Color3f cwiseExp() const {
        return {std::exp(r), std::exp(g), std::exp(b)};
    }

    /// Per-channel sRGB encode (OETF), see core/color/transfer.h.
    inline Color3f linearToSRGB() const {
        return {srgbEncode(r), srgbEncode(g), srgbEncode(b)};
    }

    /// Per-channel sRGB decode (EOTF), see core/color/transfer.h.
    inline Color3f sRGBToLinear() const {
        return {srgbDecode(r), srgbDecode(g), srgbDecode(b)};
    }

    // ── Static Factories ─────────────────────────────────────
    static constexpr Color3f black() { return {0.0f, 0.0f, 0.0f}; }
    static constexpr Color3f white() { return {1.0f, 1.0f, 1.0f}; }
    static constexpr Color3f red()   { return {1.0f, 0.0f, 0.0f}; }
    static constexpr Color3f green() { return {0.0f, 1.0f, 0.0f}; }
    static constexpr Color3f blue()  { return {0.0f, 0.0f, 1.0f}; }
};

// ── Left-Multiply Operators ──────────────────────────────────
inline constexpr Color3f operator*(float s, const Color3f& c) { return c * s; }

// ── Interpolation ────────────────────────────────────────────
inline constexpr Color3f lerp(const Color3f& a, const Color3f& b, float t) {
    return a * (1.0f - t) + b * t;
}

// ── Stream Output ────────────────────────────────────────────
inline std::ostream& operator<<(std::ostream& os, const Color3f& c) {
    return os << "Color3f(" << c.r << ", " << c.g << ", " << c.b << ")";
}

} // namespace photon
