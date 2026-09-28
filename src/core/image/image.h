#pragma once

#include "core/color/spectrum.h"

#include <vector>
#include <cstdint>
#include <cassert>
#include <algorithm>

namespace photon {

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

    // Sample accumulation (tile renderer writes disjoint pixels per pass)
    void addSample(int x, int y, const Color3f& color);
    Color3f getAveragedPixel(int x, int y) const;
    int getSampleCount(int x, int y) const;

    int width() const { return m_width; }
    int height() const { return m_height; }
    const float* data() const { return m_data.data(); }
    float* data() { return m_data.data(); }
    size_t pixelCount() const { return static_cast<size_t>(m_width) * m_height; }

private:
    int m_width = 0;
    int m_height = 0;
    std::vector<float> m_data;   // w * h * 3 (RGB)
    std::vector<int> m_sampleCounts;

    size_t pixelIndex(int x, int y) const {
        assert(x >= 0 && x < m_width && y >= 0 && y < m_height);
        return (static_cast<size_t>(y) * m_width + x) * 3;
    }
    size_t flatIndex(int x, int y) const {
        return static_cast<size_t>(y) * m_width + x;
    }
};

} // namespace photon
