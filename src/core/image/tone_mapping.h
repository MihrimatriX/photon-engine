#pragma once

/// @file tone_mapping.h
/// @brief Tone mapping operations for converting HDR images to LDR for PhotonEngine.

#include "core/color/spectrum.h"

namespace photon {

/// @brief Supported tone mapping operators
enum class ToneMapOperator {
    Reinhard,
    ReinhardExtended,
    ACES,
    Filmic
};

/// Reinhard global tone mapping: x / (1 + x)
Color3f toneMapReinhard(const Color3f& hdr);

/// Extended Reinhard tone mapping that allows mapping whites to peak output
Color3f toneMapReinhardExtended(const Color3f& hdr, float maxWhite);

/// ACES Filmic Tone Mapping approximation (standard curve used in Unreal/Unity)
Color3f toneMapACES(const Color3f& hdr);

/// Uncharted 2 Filmic tone mapping
Color3f toneMapFilmic(const Color3f& hdr);

/// Simple exposure scaling: scale by exp(exposure) or direct exposure multiplier
Color3f toneMapExposure(const Color3f& hdr, float exposure);

/// Apply gamma correction
Color3f applyGamma(const Color3f& linear, float gamma = 2.2f);

/// Main tone mapping dispatch function
Color3f toneMap(const Color3f& hdr, ToneMapOperator op, float exposure = 1.0f);

} // namespace photon
