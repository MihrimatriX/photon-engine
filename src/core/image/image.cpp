#include "image.h"
#include <cmath>

namespace photon {

// Color3f is defined entirely inline in spectrum.h

// --- Image ---

Image::Image(int width, int height) {
    resize(width, height);
}

void Image::resize(int width, int height) {
    m_width = width;
    m_height = height;

    const size_t totalPixels = static_cast<size_t>(width) * height;

    // Resize pixel data buffer (RGB floats)
    m_data.assign(totalPixels * 3, 0.0f);

    // Atomic<int> and std::mutex are not copyable/movable in the way
    // std::vector::resize requires, so we construct new vectors of the
    // correct size and move-assign them.
    m_sampleCounts = std::vector<std::atomic<int>>(totalPixels);
    m_pixelMutexes = std::vector<std::mutex>(totalPixels);
}

void Image::clear() {
    std::fill(m_data.begin(), m_data.end(), 0.0f);

    for (size_t i = 0; i < m_sampleCounts.size(); ++i) {
        m_sampleCounts[i].store(0, std::memory_order_relaxed);
    }
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

    std::lock_guard<std::mutex> lock(m_pixelMutexes[flat]);

    m_data[idx + 0] += color.r;
    m_data[idx + 1] += color.g;
    m_data[idx + 2] += color.b;

    m_sampleCounts[flat].fetch_add(1, std::memory_order_relaxed);
}

Color3f Image::getAveragedPixel(int x, int y) const {
    const size_t flat = flatIndex(x, y);
    const int count = m_sampleCounts[flat].load(std::memory_order_relaxed);

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
    return m_sampleCounts[flatIndex(x, y)].load(std::memory_order_relaxed);
}

} // namespace photon
