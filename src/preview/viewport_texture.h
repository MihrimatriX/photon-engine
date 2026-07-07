#pragma once

#include "core/image/image.h"
#include "core/image/tone_mapping.h"
#include <cstdint>

namespace photon {

class ViewportTexture {
public:
    ViewportTexture() = default;
    ~ViewportTexture();

    void init();
    void resize(int w, int h);
    void upload(const Image& img, ToneMapOperator tmo, float exposure);
    void draw(float x, float y, float w, float h) const;
    unsigned int textureId() const { return m_tex; }

private:
    unsigned int m_tex = 0;
    int m_width = 0;
    int m_height = 0;
};

} // namespace photon
