#include "samplers/stratified_sampler.h"

namespace photon {

StratifiedSampler::StratifiedSampler(int xSamples, int ySamples, uint64_t seed)
    : m_xSamples(xSamples), m_ySamples(ySamples), m_baseSeed(seed), m_rng(seed) {}

float StratifiedSampler::get1D() {
    return m_rng.uniformFloat();
}

Vec2f StratifiedSampler::get2D() {
    m_get2DCallCount++;
    
    // For the first 2D sample (normally used for pixel jitter/anti-aliasing),
    // we return a stratified sample.
    if (m_get2DCallCount == 1) {
        int xStratum = m_currentSampleIndex % m_xSamples;
        int yStratum = m_currentSampleIndex / m_xSamples;
        
        float u = (xStratum + m_rng.uniformFloat()) / static_cast<float>(m_xSamples);
        float v = (yStratum + m_rng.uniformFloat()) / static_cast<float>(m_ySamples);
        
        return {u, v};
    }
    
    // Fallback to independent random sampling for high dimensions
    return m_rng.uniformFloat2D();
}

std::unique_ptr<Sampler> StratifiedSampler::clone(uint64_t seed) const {
    return std::make_unique<StratifiedSampler>(m_xSamples, m_ySamples, seed);
}

void StratifiedSampler::startPixel(int x, int y) {
    m_pixelX = x;
    m_pixelY = y;
    m_currentSampleIndex = 0;
    
    // Cohrent seed per pixel
    uint64_t pixelSeed = m_baseSeed ^ (static_cast<uint64_t>(x) * 19123 + static_cast<uint64_t>(y) * 92183);
    m_rng = RNG(pixelSeed);
}

void StratifiedSampler::startSample(int sampleIndex) {
    m_currentSampleIndex = sampleIndex;
    m_get2DCallCount = 0;
    
    // Seed based on pixel and sample index to keep deterministic stream
    uint64_t sampleSeed = m_baseSeed ^ 
        (static_cast<uint64_t>(m_pixelX) * 19123 + 
         static_cast<uint64_t>(m_pixelY) * 92183 + 
         static_cast<uint64_t>(sampleIndex) * 239811);
    m_rng = RNG(sampleSeed);
}

} // namespace photon
