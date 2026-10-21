// transfer.h — sRGB aktarım fonksiyonları (encode/decode) ve 8-bit nicemleme + TPDF dither.
// Renderer doğrusal ışıkla çalışır; 8-bit dosyalar ve ekran ise algısal olarak düzgün
// (gamma benzeri) kodlama bekler. Bu iki dünya arasındaki köprü burasıdır.
#pragma once

/// @file transfer.h
/// @brief sRGB transfer functions and 8-bit quantization.
///
/// The renderer works in linear Rec.709/sRGB primaries. Two curves connect it to
/// 8-bit files and the screen (IEC 61966-2-1):
///   - decode (EOTF):  encoded value in a PNG/JPG  -> linear light
///   - encode (OETF):  linear light                -> value stored in a PNG / sent to the display
/// Both are piecewise: a short linear toe near black and a 2.4 power above it.
/// A plain pow(x, 1/2.2) is close in the mid-tones but wrong near black.

#include <cstdint>

namespace photon {

/// sRGB decode: encoded value in [0, 1] -> linear. Exact piecewise curve.
float srgbDecode(float encoded);

/// sRGB encode: linear value -> encoded value in [0, 1]. Input is clamped to [0, 1].
float srgbEncode(float linear);

/// Round a [0, 1] value to 8 bits. @p dither is added in LSB units before
/// rounding (pass 0 for plain round-to-nearest).
uint8_t quantizeUnorm8(float value, float dither = 0.0f);

/// Triangular-PDF dither in (-1, 1) LSB, a pure function of pixel and channel.
/// Hides 8-bit banding in smooth gradients and keeps exports reproducible.
float tpdfDither(int x, int y, int channel);

} // namespace photon
