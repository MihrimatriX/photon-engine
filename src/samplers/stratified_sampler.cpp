// stratified_sampler.cpp — Tabakalı (jittered) örnekleme: [0,1)² alanı nx × ny eşit hücreye
// (tabaka/stratum) bölünür; i. örnek i. hücreye düşer ve hücre içinde rastgele kaydırılır.
// Böylece N örnek alanı kümelenmeden kaplar; varyans bağımsız örneklemeden asla büyük
// olmaz, düzgün integrandlarda belirgin biçimde küçülür (PBRT 4. baskı, bölüm 8.5).
#include "samplers/stratified_sampler.h"

namespace photon {

StratifiedSampler::StratifiedSampler(int xSamples, int ySamples, uint64_t seed)
    : m_xSamples(xSamples < 1 ? 1 : xSamples)
    , m_ySamples(ySamples < 1 ? 1 : ySamples)
    , m_baseSeed(seed)
    , m_rng(seed) {}

// 1B: n = nx·ny tabaka; değer = (tabaka + ξ) / n, ξ ∈ [0,1) hücre içi jitter.
// Tabaka indeksi örnek indeksi + boyut kadar kaydırılır ki farklı boyutlar aynı örnekte
// hep aynı tabakaya düşüp birbirleriyle ilişkili (korelasyonlu) olmasın.
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
    // Doğrusal tabaka indeksi s → 2B hücre (s mod nx, s div nx); hücre içinde jitter.
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

// Her (piksel, örnek) çifti için RNG yeniden tohumlanır: örnek i'nin sayıları önceki
// örneklerde kaç çağrı yapıldığından bağımsız olur → ilerlemeli render tekrarlanabilir.
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
