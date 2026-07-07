#include "image.h"
#include <cmath>

namespace photon {

Image::Image(int width, int height) {
    resize(width, height);
}

void Image::resize(int width, int height) {
    m_width = width;
    m_height = height;

    const size_t totalPixels = static_cast<size_t>(width) * height;
    m_data.assign(totalPixels * 3, 0.0f);
    m_sampleCounts.assign(totalPixels, 0);
}

void Image::clear() {
    std::fill(m_data.begin(), m_data.end(), 0.0f);
    std::fill(m_sampleCounts.begin(), m_sampleCounts.end(), 0);
}

void Image::setPixel(int x, int y, const Color3f& color) {
    const size_t idx = pixelIndex(x, y);
    m_data[idx + 0] = color.r;
    m_data[idx + 1] = color.g;
    m_data[idx + 2] = color.b;
}

Color3f Image::getPixel(int x, int y) const {
    const size_t idx = pixelIndex(x, y);
    return Color3f(m_data[idx + 0], m_data[idx + 1], m_data[idx + 2]);
}

void Image::addSample(int x, int y, const Color3f& color) {
    const size_t flat = flatIndex(x, y);
    const size_t idx = flat * 3;

    m_data[idx + 0] += color.r;
    m_data[idx + 1] += color.g;
    m_data[idx + 2] += color.b;
    ++m_sampleCounts[flat];
}

Color3f Image::getAveragedPixel(int x, int y) const {
    const size_t flat = flatIndex(x, y);
    const int count = m_sampleCounts[flat];

    if (count <= 0) {
        return Color3f::black();
    }

    const size_t idx = flat * 3;
    const float inv = 1.0f / static_cast<float>(count);

    return Color3f(
        m_data[idx + 0] * inv,
        m_data[idx + 1] * inv,
        m_data[idx + 2] * inv
    );
}

int Image::getSampleCount(int x, int y) const {
    return m_sampleCounts[flatIndex(x, y)];
}

} // namespace photon
