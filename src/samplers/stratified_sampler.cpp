#include "samplers/stratified_sampler.h"

namespace photon {

StratifiedSampler::StratifiedSampler(int xSamples, int ySamples, uint64_t seed)
    : m_xSamples(xSamples < 1 ? 1 : xSamples)
    , m_ySamples(ySamples < 1 ? 1 : ySamples)
    , m_baseSeed(seed)
    , m_rng(seed) {}

float StratifiedSampler::get1D() {
    int dim = m_get1DCallCount++;
    int n = m_xSamples * m_ySamples;
    int stratum = (m_currentSampleIndex % n + dim) % n;
    return (static_cast<float>(stratum) + m_rng.uniformFloat()) / static_cast<float>(n);
}

Vec2f StratifiedSampler::get2D() {
    int dim = m_get2DCallCount++;
    int n = m_xSamples * m_ySamples;
    // dim shifts the stratum; (s + dim) mod n is a bijection, so each dimension is stratified.
    int s = (m_currentSampleIndex % n + dim) % n;
    int xStratum = s % m_xSamples;
    int yStratum = s / m_xSamples;
    float u = (static_cast<float>(xStratum) + m_rng.uniformFloat()) / static_cast<float>(m_xSamples);
    float v = (static_cast<float>(yStratum) + m_rng.uniformFloat()) / static_cast<float>(m_ySamples);
    return {u, v};
}

std::unique_ptr<Sampler> StratifiedSampler::clone(uint64_t seed) const {
    return std::make_unique<StratifiedSampler>(m_xSamples, m_ySamples, seed);
}

void StratifiedSampler::startPixel(int x, int y) {
    m_pixelX = x;
    m_pixelY = y;
    m_currentSampleIndex = 0;
    m_get1DCallCount = 0;
    m_get2DCallCount = 0;

    uint64_t pixelSeed = m_baseSeed ^ (static_cast<uint64_t>(x) * 19123 + static_cast<uint64_t>(y) * 92183);
    m_rng = RNG(pixelSeed);
}

void StratifiedSampler::startSample(int sampleIndex) {
    m_currentSampleIndex = sampleIndex;
    m_get1DCallCount = 0;
    m_get2DCallCount = 0;

    uint64_t sampleSeed = m_baseSeed ^
        (static_cast<uint64_t>(m_pixelX) * 19123 +
         static_cast<uint64_t>(m_pixelY) * 92183 +
         static_cast<uint64_t>(sampleIndex) * 239811);
    m_rng = RNG(sampleSeed);
}

} // namespace photon
