#pragma once

/// @file image_io.h
/// @brief Image loading and saving utilities for PhotonEngine.

#include "core/image/image.h"
#include "core/image/tone_mapping.h"
#include <string>
#include <optional>

namespace photon {

/// Save Image to PNG file (applies exposure, tone mapping and gamma correction)
bool saveImagePNG(const Image& img, const std::string& path, ToneMapOperator tmo = ToneMapOperator::ACES, float exposure = 1.0f);

/// Save raw HDR Image to OpenEXR file
bool saveImageEXR(const Image& img, const std::string& path);

/// Load HDR image (from Radiance .hdr format)
std::optional<Image> loadImageHDR(const std::string& path);

/// Load EXR image (from OpenEXR .exr format)
std::optional<Image> loadImageEXR(const std::string& path);

/// Load LDR image (from PNG/JPG/TGA format, converts sRGB to linear automatically)
std::optional<Image> loadImageLDR(const std::string& path);

} // namespace photon
