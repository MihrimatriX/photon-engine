#pragma once

#include "core/image/image.h"
#include "core/image/tone_mapping.h"
#include <cstdint>

namespace photon {

/// GL texture holding the display-encoded path-traced image.
/// Texture row 0 is the image's top row: draw it with ImGui UVs (0,0)-(1,1).
class ViewportTexture {
public:
    ViewportTexture() = default;
    ~ViewportTexture();

    void init();
    /// Tone map, sRGB-encode, dither and upload a resolved HDR image.
    void upload(const Image& img, ToneMapOperator tmo, float exposureEV);
    unsigned int textureId() const { return m_tex; }

private:
    unsigned int m_tex = 0;
};

} // namespace photon
