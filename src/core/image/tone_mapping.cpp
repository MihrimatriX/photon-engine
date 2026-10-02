#include "core/image/tone_mapping.h"
#include <cmath>
#include <algorithm>

namespace photon {

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
    // Narkowicz 2015 ACES Fitted approximation
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
    // Uncharted 2 Hable Filmic curve
    auto hableFilmic = [](float x) {
        float A = 0.15f;
        float B = 0.50f;
        float C = 0.10f;
        float D = 0.20f;
        float E = 0.02f;
        float F = 0.30f;
        return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
    };
    
    float W = 11.2f; // White point scale
    float whiteScale = 1.0f / hableFilmic(W);
    
    return Color3f(
        hableFilmic(hdr.r) * whiteScale,
        hableFilmic(hdr.g) * whiteScale,
        hableFilmic(hdr.b) * whiteScale
    );
}

Color3f toneMapExposure(const Color3f& hdr, float exposureEV) {
    float scale = std::exp2(exposureEV);
    return hdr * scale;
}

Color3f toneMapLinear(const Color3f& hdr, ToneMapOperator op, float exposureEV) {
    Color3f exposed = toneMapExposure(hdr, exposureEV);
    // NaN fails every comparison, so max(0, NaN) style clamping is not enough.
    auto sane = [](float v) { return finiteFloat(v) && v > 0.0f ? v : 0.0f; };
    exposed = Color3f(sane(exposed.r), sane(exposed.g), sane(exposed.b));
    Color3f ldr;
    switch (op) {
        case ToneMapOperator::Reinhard:
            ldr = toneMapReinhard(exposed);
            break;
        case ToneMapOperator::ReinhardExtended:
            ldr = toneMapReinhardExtended(exposed, 4.0f);
            break;
        case ToneMapOperator::ACES:
            ldr = toneMapACES(exposed);
            break;
        case ToneMapOperator::Filmic:
            ldr = toneMapFilmic(exposed);
            break;
    }
    return ldr.clamp(0.0f, 1.0f);
}

Color3f toneMap(const Color3f& hdr, ToneMapOperator op, float exposureEV) {
    return toneMapLinear(hdr, op, exposureEV).linearToSRGB();
}

} // namespace photon
