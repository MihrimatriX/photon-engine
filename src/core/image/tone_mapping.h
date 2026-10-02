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

/// Exposure in EV stops: scales by 2^exposureEV (0 = unchanged, +1 = twice as bright).
Color3f toneMapExposure(const Color3f& hdr, float exposureEV);

/// Exposure and the operator's curve only. Output is display-linear in [0, 1],
/// not yet sRGB-encoded. Negative and NaN inputs map to 0.
Color3f toneMapLinear(const Color3f& hdr, ToneMapOperator op, float exposureEV = 0.0f);

/// The full display transform: exposure -> operator -> sRGB encode (OETF).
/// Output is the [0, 1] value that goes into an 8-bit PNG or the viewport.
Color3f toneMap(const Color3f& hdr, ToneMapOperator op, float exposureEV = 0.0f);

} // namespace photon
