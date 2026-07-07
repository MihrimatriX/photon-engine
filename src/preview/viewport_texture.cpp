#include "preview/viewport_texture.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>
#include <vector>
#include <algorithm>

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

void ViewportTexture::resize(int w, int h) {
    m_width = w;
    m_height = h;
}

void ViewportTexture::upload(const Image& img, ToneMapOperator tmo, float exposure) {
    if (!m_tex) init();
    if (img.width() != m_width || img.height() != m_height) {
        resize(img.width(), img.height());
    }
    std::vector<uint8_t> pixels(static_cast<size_t>(img.width() * img.height() * 3));
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            Color3f hdr = img.getAveragedPixel(x, y);
            Color3f ldr = toneMap(hdr, tmo, exposure);
            ldr = applyGamma(ldr, 2.2f);
            size_t i = static_cast<size_t>((y * img.width() + x) * 3);
            pixels[i] = static_cast<uint8_t>(std::clamp(ldr.r * 255.0f, 0.0f, 255.0f));
            pixels[i + 1] = static_cast<uint8_t>(std::clamp(ldr.g * 255.0f, 0.0f, 255.0f));
            pixels[i + 2] = static_cast<uint8_t>(std::clamp(ldr.b * 255.0f, 0.0f, 255.0f));
        }
    }
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, img.width(), img.height(), 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ViewportTexture::draw(float x, float y, float w, float h) const {
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 1); glVertex2f(x, y);
    glTexCoord2f(1, 1); glVertex2f(x + w, y);
    glTexCoord2f(1, 0); glVertex2f(x + w, y + h);
    glTexCoord2f(0, 0); glVertex2f(x, y + h);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

} // namespace photon
