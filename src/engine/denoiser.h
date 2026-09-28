#pragma once

/// @file denoiser.h
/// @brief Optional Intel OIDN image denoiser + soft blur fallback.

#include "core/image/image.h"

namespace photon {

/// Denoise an HDR color buffer in-place.
/// Uses Intel OIDN when linked (PHOTON_ENABLE_OIDN). Otherwise applies a
/// cheap firefly-suppressing blur so "Denoise" still does something for full-res.
/// Optional albedo/normal AOVs improve OIDN quality when provided (same resolution).
bool denoiseImage(Image& color, const Image* albedo = nullptr, const Image* normal = nullptr);

/// True when this build was compiled with a linked OIDN SDK.
bool denoiseAvailable();

} // namespace photon
