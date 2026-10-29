// pixel_filter.cpp — Piksel filtreleri ve ters CDF tablosuyla filtre önem örneklemesi.
#include "core/image/pixel_filter.h"
#include "core/math/constants.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace photon {

namespace {

constexpr int kBins = 512;

} // namespace

bool parsePixelFilter(const char* name, PixelFilterType& out) {
    if (!name) return false;
    if (!std::strcmp(name, "box")) out = PixelFilterType::Box;
    else if (!std::strcmp(name, "triangle") || !std::strcmp(name, "tent")) out = PixelFilterType::Triangle;
    else if (!std::strcmp(name, "gaussian") || !std::strcmp(name, "gauss")) out = PixelFilterType::Gaussian;
    else if (!std::strcmp(name, "blackman-harris") || !std::strcmp(name, "bh")) out = PixelFilterType::BlackmanHarris;
    else return false;
    return true;
}

const char* pixelFilterName(PixelFilterType type) {
    switch (type) {
        case PixelFilterType::Box: return "box";
        case PixelFilterType::Triangle: return "triangle";
        case PixelFilterType::Gaussian: return "gaussian";
        case PixelFilterType::BlackmanHarris: return "blackman-harris";
    }
    return "box";
}

PixelFilterSampler::PixelFilterSampler(const PixelFilter& filter) : m_filter(filter) {
    m_filter.width = std::clamp(m_filter.width, 0.25f, 8.0f);
    m_radius = 0.5f * m_filter.width;
    if (m_filter.type == PixelFilterType::Box) return;
    // Hücre başına orta nokta değeriyle parçalı sabit pdf; CDF düğümlerde.
    m_cdf.resize(kBins + 1);
    m_cdf[0] = 0.0f;
    const float dx = m_filter.width / kBins;
    double acc = 0.0;
    for (int i = 0; i < kBins; ++i) {
        acc += std::max(0.0f, evaluate(-m_radius + (static_cast<float>(i) + 0.5f) * dx));
        m_cdf[static_cast<size_t>(i) + 1] = static_cast<float>(acc);
    }
    if (!(acc > 0.0)) {
        m_cdf.clear();
        m_filter.type = PixelFilterType::Box;
        return;
    }
    for (float& c : m_cdf) c = static_cast<float>(c / acc);
    m_cdf.back() = 1.0f;
}

float PixelFilterSampler::evaluate(float x) const {
    const float r = m_radius;
    if (std::abs(x) > r) return 0.0f;
    switch (m_filter.type) {
        case PixelFilterType::Box: return 1.0f;
        case PixelFilterType::Triangle: return std::max(0.0f, 1.0f - std::abs(x) / r);
        case PixelFilterType::Gaussian: {
            // σ = genişlik/6: ±3σ kenarda; kenar değeri çıkarılır ki filtre sürekli sıfıra insin.
            const float s = m_filter.width / 6.0f;
            const float k = 1.0f / (2.0f * s * s);
            return std::max(0.0f, std::exp(-k * x * x) - std::exp(-k * r * r));
        }
        case PixelFilterType::BlackmanHarris: {
            const float a = TWO_PI * (x / m_filter.width + 0.5f);
            return 0.35875f - 0.48829f * std::cos(a) + 0.14128f * std::cos(2.0f * a) - 0.01168f * std::cos(3.0f * a);
        }
    }
    return 0.0f;
}

float PixelFilterSampler::sample1D(float u) const {
    if (m_cdf.empty()) return (u - 0.5f) * m_filter.width;
    // CDF'de u'yu içeren hücre, içinde doğrusal (parçalı sabit pdf'in tam ters dönüşümü).
    const auto it = std::upper_bound(m_cdf.begin(), m_cdf.end(), u);
    const int i = std::clamp(static_cast<int>(it - m_cdf.begin()) - 1, 0, kBins - 1);
    const float c0 = m_cdf[static_cast<size_t>(i)], c1 = m_cdf[static_cast<size_t>(i) + 1];
    const float t = c1 > c0 ? (u - c0) / (c1 - c0) : 0.5f;
    return -m_radius + (static_cast<float>(i) + std::clamp(t, 0.0f, 1.0f)) * (m_filter.width / kBins);
}

} // namespace photon
