#include "core/color/transfer.h"

#include <algorithm>
#include <cmath>

namespace photon {

float srgbDecode(float encoded) {
    if (encoded <= 0.04045f) return encoded / 12.92f;
    return std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

float srgbEncode(float linear) {
    linear = std::clamp(linear, 0.0f, 1.0f);
    // TODO(human): F1.12 — write the piecewise sRGB OETF (IEC 61966-2-1), the
    // inverse of srgbDecode above:
    //   linear <= 0.0031308  ->  12.92 * linear
    //   otherwise            ->  1.055 * linear^(1/2.4) - 0.055
    // The line below is the old display curve (pure gamma 2.2), kept so the app
    // looks the same until this is written. It is too bright near black:
    // 0.001 encodes to 0.043 instead of 0.0129.
    // Green when: ctest -L todo_human  (tests/learning/test_srgb_oetf.cpp)
    return std::pow(linear, 1.0f / 2.2f);
}

uint8_t quantizeUnorm8(float value, float dither) {
    float v = std::floor(value * 255.0f + dither + 0.5f);
    return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f));
}

float tpdfDither(int x, int y, int channel) {
    // Two independent uniforms from one 32-bit hash (lowbias32, Wellons).
    uint32_t h = static_cast<uint32_t>(x) * 0x8da6b343u ^
                 static_cast<uint32_t>(y) * 0xd8163841u ^
                 static_cast<uint32_t>(channel) * 0xcb1ab31fu;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    const float u0 = static_cast<float>(h & 0xffffu) * (1.0f / 65536.0f);
    const float u1 = static_cast<float>(h >> 16) * (1.0f / 65536.0f);
    return u0 - u1;
}

} // namespace photon
