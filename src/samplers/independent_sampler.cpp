// independent_sampler.cpp — Bağımsız örnekleyici: her sayı PCG32'den doğrudan gelir.
// En basit ve yansız seçenek; ama noktalar kümelenebilir/boşluk bırakabilir, bu yüzden
// gürültü N örnekte ~1/√N hızında azalır (düşük tutarsızlıklı dizilerden daha yavaş).
#include "samplers/independent_sampler.h"

namespace photon {

IndependentSampler::IndependentSampler(uint64_t seed)
    : m_rng(seed), m_baseSeed(seed) {}

float IndependentSampler::get1D() {
    return m_rng.uniformFloat();
}

Vec2f IndependentSampler::get2D() {
    return m_rng.uniformFloat2D();
}

std::unique_ptr<Sampler> IndependentSampler::clone(uint64_t seed) const {
    return std::make_unique<IndependentSampler>(seed);
}

void IndependentSampler::startPixel(int x, int y) {
    // Re-seed RNG based on pixel coordinates to keep frames coherent or distinct
    // Piksel koordinatlarını büyük sabitlerle çarpıp tabana XOR'layarak pikseller arası
    // farklı ve tekrarlanabilir bir tohum elde edilir (aynı tohum → aynı görüntü).
    uint64_t pixelSeed = m_baseSeed ^ (static_cast<uint64_t>(x) * 19123 + static_cast<uint64_t>(y) * 92183);
    m_rng = RNG(pixelSeed);
}

void IndependentSampler::startSample(int /*sampleIndex*/) {
    // No-op for independent sampler as it just continues random stream
}

} // namespace photon
