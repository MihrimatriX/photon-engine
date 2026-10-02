#pragma once

/// @file denoiser.h
/// @brief Optional Intel OIDN image denoiser + soft blur fallback.

#include "core/image/image.h"

namespace photon {

/// Denoise a resolved (averaged) HDR image in place.
/// Uses Intel OIDN when linked (PHOTON_ENABLE_OIDN). Otherwise applies a 3x3
/// luminance-weighted blur: it softens fireflies but is not a denoiser.
/// Optional albedo/normal AOVs improve OIDN quality when provided (same resolution).
bool denoiseImage(Image& color, const Image* albedo = nullptr, const Image* normal = nullptr);

/// Denoise a copy; the source is left alone.
Image denoiseCopy(const Image& color);

/// True when this build was compiled with a linked OIDN SDK.
bool denoiseAvailable();

} // namespace photon
