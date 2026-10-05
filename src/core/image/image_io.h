#pragma once

/// @file image_io.h
/// @brief Image loading and saving utilities for PhotonEngine.
///
/// All paths are UTF-8. All images are top row first.

#include "core/image/image.h"
#include "core/image/tone_mapping.h"
#include <string>
#include <optional>
#include <cstdint>
#include <vector>

namespace photon {

/// How the 8-bit values of an LDR file map to numbers.
enum class TextureEncoding {
    SRGB,   ///< Color: base color, emission. Decoded with the sRGB EOTF.
    Linear, ///< Data: normal, roughness, metalness, masks. Value / 255, no curve.
};

/// Save a resolved image as 8-bit PNG: exposure (EV) -> tone map -> sRGB encode
/// -> round to 8 bits. With @p dither, a deterministic ±1 LSB triangular dither
/// is added before rounding to hide banding.
bool saveImagePNG(const Image& img, const std::string& path,
                  ToneMapOperator tmo = ToneMapOperator::ACES, float exposureEV = 0.0f,
                  bool dither = true);

/// Şeffaf arka planlı 8-bit RGBA PNG. @p img önceden çarpılmış (premultiplied)
/// renk, @p alpha kapsama (r kanalı). Renk alfaya bölünüp ton eşlenir.
bool saveImagePNGAlpha(const Image& img, const Image& alpha, const std::string& path,
                       ToneMapOperator tmo, float exposureEV);

/// 8-bit JPEG (kalite 1–100). Alfa yok.
bool saveImageJPG(const Image& img, const std::string& path, ToneMapOperator tmo,
                  float exposureEV, int quality = 95);

/// Hazır RGBA8 tamponu PNG olarak yaz (ekran görüntüsü için). OpenGL'den okunan
/// tamponlar alttan başladığı için @p flipVertically genelde true verilir.
bool saveRGBA8PNG(const uint8_t* rgba, int w, int h, const std::string& path, bool flipVertically);

/// PNG/JPG dosyasını ham RGBA8 olarak oku (renk dönüşümü yok; küçük resim önbelleği için).
bool loadRGBA8(const std::string& path, std::vector<uint8_t>& out, int& w, int& h);

/// Save scene-linear radiance as OpenEXR (half float RGB, ZIP compression).
bool saveImageEXR(const Image& img, const std::string& path);

/// Load a Radiance .hdr file. Fails for any non-HDR format.
std::optional<Image> loadImageHDR(const std::string& path);

/// Load EXR image (from OpenEXR .exr format)
std::optional<Image> loadImageEXR(const std::string& path);

/// Load an 8-bit PNG/JPG/TGA/BMP file. Fails for .hdr (use loadImageHDR).
std::optional<Image> loadImageLDR(const std::string& path,
                                  TextureEncoding encoding = TextureEncoding::SRGB);

/// Decode a PNG/JPG/TGA blob already in memory. Empty if stb cannot read it.
std::optional<Image> loadImageLDRMemory(const unsigned char* bytes, int size,
                                        TextureEncoding encoding = TextureEncoding::SRGB);

} // namespace photon
