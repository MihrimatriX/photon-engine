// transfer.cpp — sRGB aktarım eğrileri (doğrusal ↔ kodlanmış) ve 8-bit nicemleme + titreşim
// (dither). PNG'ye yazarken ve ekrana gönderirken doğrusal ışık bu dosyadan geçer.

#include "core/color/transfer.h"

#include <algorithm>
#include <cmath>

namespace photon {

// sRGB EOTF (dosyadaki değer → doğrusal ışık). Doku/renk seçici değerleri sRGB kodludur;
// ışık hesabından önce doğrusala çevrilmeleri gerekir, yoksa karışımlar koyu/yanlış çıkar.
float srgbDecode(float encoded) {
    if (encoded <= 0.04045f) return encoded / 12.92f;
    return std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

float srgbEncode(float linear) {
    linear = std::clamp(linear, 0.0f, 1.0f);
    // Parçalı sRGB OETF (IEC 61966-2-1), srgbDecode'un tam tersi:
    //   linear ≤ 0.0031308 → 12.92·linear            (siyaha yakın doğrusal "ayak")
    //   aksi halde         → 1.055·linear^(1/2.4) - 0.055
    // İki parça 0.0031308'de hem değer hem eğim olarak (yaklaşık) birleşir. Saf
    // gamma 2.2 (eski kod) siyaha yakın çok parlaktı: 0.001 → 0.043 (doğrusu 0.0129).
    if (linear <= 0.0031308f) return 12.92f * linear;
    return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

// floor(x·255 + 0.5) en yakına yuvarlamadır; dither yuvarlamadan önce LSB birimiyle
// eklenir, böylece nicemleme hatası sinyalden bağımsız gürültüye dönüşür.
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
    // İki bağımsız U[0,1) farkı üçgen dağılım verir (iki dikdörtgenin konvolüsyonu):
    // aralık (−1, 1), tepe 0'da. TPDF dither, düzgün dither'dan farklı olarak gürültü
    // gücünü sinyalden bağımsız kılar → gradyanlarda bantlanma (banding) görünmez.
    const float u0 = static_cast<float>(h & 0xffffu) * (1.0f / 65536.0f);
    const float u1 = static_cast<float>(h >> 16) * (1.0f / 65536.0f);
    return u0 - u1;
}

} // namespace photon
