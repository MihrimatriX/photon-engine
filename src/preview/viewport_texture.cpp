#include "preview/viewport_texture.h"
#include "core/color/transfer.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>
#include <vector>

namespace photon {

ViewportTexture::~ViewportTexture() {
    if (m_tex) glDeleteTextures(1, &m_tex);
}

void ViewportTexture::init() {
    glGenTextures(1, &m_tex);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ViewportTexture::upload(const Image& img, ToneMapOperator tmo, float exposureEV) {
    if (!m_tex) init();
    const int w = img.width();
    const int h = img.height();
    if (w <= 0 || h <= 0) return;
    // RGBA so rows stay 4-byte aligned (GL_UNPACK_ALIGNMENT default is 4). Tight RGB
    // makes the driver read past the buffer and corrupt the heap; ImGui then dies
    // in ImDrawListSplitter::SetCurrentChannel.
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Color3f display = toneMap(img.getPixel(x, y), tmo, exposureEV);
            const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4;
            for (int c = 0; c < 3; ++c) {
                pixels[i + static_cast<size_t>(c)] = quantizeUnorm8(display[c], tpdfDither(x, y, c));
            }
            pixels[i + 3] = 255;
        }
    }
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace photon
