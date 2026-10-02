#pragma once

#include "core/color/spectrum.h"

#include <vector>
#include <cstdint>
#include <cassert>
#include <algorithm>

namespace photon {

/// Plain float RGB image: textures, environment maps, resolved renders, AOVs.
/// Rows are stored top to bottom: (0, 0) is the top-left pixel.
/// Sample accumulation lives in Film (core/image/film.h).
class Image {
public:
    Image() = default;
    Image(int width, int height);

    void resize(int width, int height);
    void clear();

    void setPixel(int x, int y, const Color3f& color);
    Color3f getPixel(int x, int y) const;

    /// Bilinear sample with wrap UV in [0,1]. Uses raw pixel values (not accumulation averages).
    Color3f sampleBilinear(float u, float v) const;

    int width() const { return m_width; }
    int height() const { return m_height; }
    const float* data() const { return m_data.data(); }
    float* data() { return m_data.data(); }
    size_t pixelCount() const { return static_cast<size_t>(m_width) * m_height; }

private:
    int m_width = 0;
    int m_height = 0;
    std::vector<float> m_data;   // w * h * 3 (RGB)

    size_t pixelIndex(int x, int y) const {
        assert(x >= 0 && x < m_width && y >= 0 && y < m_height);
        return (static_cast<size_t>(y) * m_width + x) * 3;
    }
};

} // namespace photon
