// tone_mapping.cpp — Ton eşleme eğrilerinin uygulaması.
#include "core/image/tone_mapping.h"
#include "core/color/transfer.h"
#include <cmath>
#include <algorithm>

namespace photon {

const char* toneMapName(ToneMapOperator op) {
    switch (op) {
        case ToneMapOperator::Reinhard: return "Reinhard";
        case ToneMapOperator::ReinhardExtended: return "Reinhard (beyaz noktalı)";
        case ToneMapOperator::ACES: return "ACES Filmic";
        case ToneMapOperator::Filmic: return "Hable Filmic";
        case ToneMapOperator::AgX: return "AgX";
        case ToneMapOperator::PBRNeutral: return "PBR Nötr (ürün)";
        case ToneMapOperator::Linear: return "Doğrusal";
    }
    return "?";
}

Color3f toneMapReinhard(const Color3f& hdr) {
    return Color3f(
        hdr.r / (1.0f + hdr.r),
        hdr.g / (1.0f + hdr.g),
        hdr.b / (1.0f + hdr.b)
    );
}

Color3f toneMapReinhardExtended(const Color3f& hdr, float maxWhite) {
    float maxWhite2 = maxWhite * maxWhite;
    auto mapChannel = [maxWhite2](float x) {
        return (x * (1.0f + x / maxWhite2)) / (1.0f + x);
    };
    return Color3f(mapChannel(hdr.r), mapChannel(hdr.g), mapChannel(hdr.b));
}

Color3f toneMapACES(const Color3f& hdr) {
    // Narkowicz 2015 ACES fit: rasyonel bir eğri, kanal başına uygulanır.
    auto acesCurve = [](float x) {
        float a = 2.51f;
        float b = 0.03f;
        float c = 2.43f;
        float d = 0.59f;
        float e = 0.14f;
        return std::clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
    };
    return Color3f(acesCurve(hdr.r), acesCurve(hdr.g), acesCurve(hdr.b));
}

Color3f toneMapFilmic(const Color3f& hdr) {
    // Uncharted 2 Hable eğrisi; W = beyaz noktası.
    auto hableFilmic = [](float x) {
        float A = 0.15f;
        float B = 0.50f;
        float C = 0.10f;
        float D = 0.20f;
        float E = 0.02f;
        float F = 0.30f;
        return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
    };

    float W = 11.2f;
    float whiteScale = 1.0f / hableFilmic(W);

    return Color3f(
        hableFilmic(hdr.r) * whiteScale,
        hableFilmic(hdr.g) * whiteScale,
        hableFilmic(hdr.b) * whiteScale
    );
}

// AgX: renk önce "içe doğru" bir matrisle hafifçe karıştırılır (aşırı doygun
// renkler beyaza doğru yumuşakça kayabilsin diye), log2 uzayında [-12.47, +4.03]
// EV aralığına normalize edilir, bir S-eğrisi (6. derece polinom) uygulanır ve
// ters matrisle geri döndürülür. Eğri sRGB kodlu değer üretir; motorun geri
// kalanı ekran-doğrusal beklediği için sonunda sRGB çözülür.
Color3f toneMapAgX(const Color3f& hdr) {
    auto mul = [](const float m[9], const Color3f& v) {
        // GLSL mat3 sütun-öncelikli: m[0..2] birinci sütun.
        return Color3f(m[0] * v.r + m[3] * v.g + m[6] * v.b,
                       m[1] * v.r + m[4] * v.g + m[7] * v.b,
                       m[2] * v.r + m[5] * v.g + m[8] * v.b);
    };
    static const float inset[9] = {
        0.842479062253094f, 0.0423282422610123f, 0.0423756549057051f,
        0.0784335999999992f, 0.878468636469772f, 0.0784336f,
        0.0792237451477643f, 0.0791661274605434f, 0.879142973793104f};
    static const float outset[9] = {
        1.19687900512017f, -0.0528968517574562f, -0.0529716355144438f,
        -0.0980208811401368f, 1.15190312990417f, -0.0980434501171241f,
        -0.0990297440797205f, -0.0989611768448433f, 1.15107367264116f};
    static constexpr float minEv = -12.47393f;
    static constexpr float maxEv = 4.026069f;

    Color3f v = mul(inset, hdr);
    auto curve = [](float x) {
        x = std::clamp(x, minEv, maxEv);
        x = (x - minEv) / (maxEv - minEv);
        const float x2 = x * x;
        const float x4 = x2 * x2;
        return 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x +
               0.4298f * x2 + 0.1191f * x - 0.00232f;
    };
    auto lg = [](float c) { return std::log2(std::max(c, 1e-10f)); };
    v = Color3f(curve(lg(v.r)), curve(lg(v.g)), curve(lg(v.b)));
    v = mul(outset, v);
    return Color3f(srgbDecode(std::clamp(v.r, 0.0f, 1.0f)),
                   srgbDecode(std::clamp(v.g, 0.0f, 1.0f)),
                   srgbDecode(std::clamp(v.b, 0.0f, 1.0f)));
}

// Khronos PBR Neutral: 0.76'nın altındaki değerlere neredeyse dokunmaz (taban
// renkleri birebir kalır), üstünü yumuşak bir eğriyle sıkıştırır ve çok parlak
// bölgeleri hafifçe beyaza doğru doygunluktan arındırır.
Color3f toneMapPBRNeutral(const Color3f& hdr) {
    constexpr float startCompression = 0.8f - 0.04f;
    constexpr float desaturation = 0.15f;
    Color3f c = hdr;
    float x = std::min(c.r, std::min(c.g, c.b));
    float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
    c = c - Color3f(offset);
    float peak = std::max(c.r, std::max(c.g, c.b));
    if (peak < startCompression) return c;
    constexpr float d = 1.0f - startCompression;
    float newPeak = 1.0f - d * d / (peak + d - startCompression);
    c = c * (newPeak / peak);
    float g = 1.0f - 1.0f / (desaturation * (peak - newPeak) + 1.0f);
    return c * (1.0f - g) + Color3f(newPeak) * g;
}

Color3f toneMapExposure(const Color3f& hdr, float exposureEV) {
    float scale = std::exp2(exposureEV);
    return hdr * scale;
}

Color3f toneMapLinear(const Color3f& hdr, ToneMapOperator op, float exposureEV) {
    Color3f exposed = toneMapExposure(hdr, exposureEV);
    // NaN her karşılaştırmada false döner; max(0, NaN) tarzı kırpma yetmez.
    auto sane = [](float v) { return finiteFloat(v) && v > 0.0f ? v : 0.0f; };
    exposed = Color3f(sane(exposed.r), sane(exposed.g), sane(exposed.b));
    Color3f ldr;
    switch (op) {
        case ToneMapOperator::Reinhard: ldr = toneMapReinhard(exposed); break;
        case ToneMapOperator::ReinhardExtended: ldr = toneMapReinhardExtended(exposed, 4.0f); break;
        case ToneMapOperator::ACES: ldr = toneMapACES(exposed); break;
        case ToneMapOperator::Filmic: ldr = toneMapFilmic(exposed); break;
        case ToneMapOperator::AgX: ldr = toneMapAgX(exposed); break;
        case ToneMapOperator::PBRNeutral: ldr = toneMapPBRNeutral(exposed); break;
        case ToneMapOperator::Linear: ldr = exposed; break;
    }
    return ldr.clamp(0.0f, 1.0f);
}

Color3f toneMap(const Color3f& hdr, ToneMapOperator op, float exposureEV) {
    const Color3f lin = toneMapLinear(hdr, op, exposureEV);
    return Color3f(srgbEncode(lin.r), srgbEncode(lin.g), srgbEncode(lin.b));
}

} // namespace photon
