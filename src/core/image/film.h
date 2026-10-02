#pragma once

/// @file film.h
/// @brief Sample accumulator: per-pixel radiance sum and sample count.
///
/// A Film is where the renderer adds samples. It is not an image: a pixel's value
/// is sum / count, and only resolve() turns it into one. Keeping the two types
/// apart means nothing can save or display a raw sum by mistake.
///
/// Raster convention for the whole engine: (0, 0) is the TOP-left pixel.

#include "core/image/image.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace photon {

class Film {
public:
    Film() = default;
    Film(int width, int height);
    Film(const Film& other);
    Film& operator=(const Film& other);

    void resize(int width, int height);
    void clear();

    /// Add one radiance sample. A sample with a NaN, an Inf or a negative
    /// component is counted as black and recorded in rejectedSamples(): one bad
    /// path must not poison a pixel for the rest of the render.
    /// Concurrent calls are safe when they target different pixels.
    void addSample(int x, int y, const Color3f& radiance);

    /// sum / count, or black for a pixel with no samples.
    Color3f resolvedPixel(int x, int y) const;
    int sampleCount(int x, int y) const;

    /// The averaged image.
    Image resolve() const;

    int width() const { return m_width; }
    int height() const { return m_height; }
    uint64_t rejectedSamples() const { return m_rejected.load(std::memory_order_relaxed); }

private:
    int m_width = 0;
    int m_height = 0;
    std::vector<float> m_sum;        // w * h * 3
    std::vector<uint32_t> m_count;   // w * h
    std::atomic<uint64_t> m_rejected{0};

    size_t flatIndex(int x, int y) const {
        return static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x);
    }
};

} // namespace photon
