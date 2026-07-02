#pragma once

/// @file constants.h
/// @brief Compile-time mathematical constants for the PhotonEngine renderer.
///
/// All constants are defined as constexpr float values for maximum performance
/// and type safety. They are used throughout the renderer for trigonometric
/// calculations, epsilon comparisons, and angular conversions.

#include <limits>

namespace photon {

// ─────────────────────────────────────────────────────────────
// Fundamental constants
// ─────────────────────────────────────────────────────────────

/// Pi (π ≈ 3.14159265...)
constexpr float PI = 3.14159265358979323846f;

/// 2π — full circle in radians
constexpr float TWO_PI = 2.0f * PI;

/// 1/π — reciprocal of pi, used in PDF normalization
constexpr float INV_PI = 1.0f / PI;

/// 1/(2π) — reciprocal of 2π, used in spherical sampling
constexpr float INV_TWO_PI = 1.0f / TWO_PI;

/// π/2 — quarter turn (90°)
constexpr float PI_OVER_2 = PI / 2.0f;

/// π/4 — eighth turn (45°)
constexpr float PI_OVER_4 = PI / 4.0f;

/// √2 ≈ 1.41421356...
constexpr float SQRT2 = 1.41421356237309504880f;

// ─────────────────────────────────────────────────────────────
// Tolerances and limits
// ─────────────────────────────────────────────────────────────

/// Small epsilon for floating-point comparisons and ray offsets.
constexpr float EPSILON = 1e-6f;

/// Practical floating-point infinity — the largest finite float value.
/// Prefer this over std::numeric_limits<float>::infinity() to avoid
/// NaN propagation in arithmetic with actual infinities.
constexpr float INFINITY_F = std::numeric_limits<float>::max();

// ─────────────────────────────────────────────────────────────
// Angular conversion factors
// ─────────────────────────────────────────────────────────────

/// Multiply by this to convert degrees → radians.
constexpr float DEG_TO_RAD = PI / 180.0f;

/// Multiply by this to convert radians → degrees.
constexpr float RAD_TO_DEG = 180.0f / PI;

} // namespace photon
