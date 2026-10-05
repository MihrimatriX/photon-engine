// Burley 2020 hash tabanlı Owen karıştırmalı Sobol örnekleyicisinin uygulaması.
// Yalnızca Sobol'ün ilk iki boyutu kullanılır (bit ters çevirme + Pascal üçgeni
// üreteci); daha yüksek boyutlar her çağrıda bağımsız karıştırma ile "dolgu" yapılır.

#include "samplers/sobol_sampler.h"

namespace photon {

namespace {

/// 32 bitin sırasını ters çevirir (bit 0 ↔ bit 31, ...). Böl-ve-yönet: önce komşu
/// bitler, sonra 2'li, 4'lü, 8'li, 16'lı gruplar yer değiştirir.
uint32_t reverseBits(uint32_t x) {
    x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
    x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
    x = ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);
    x = ((x >> 8) & 0x00FF00FFu) | ((x & 0x00FF00FFu) << 8);
    return (x >> 16) | (x << 16);
}

/// Sobol boyut 0 = van der Corput dizisi: indeksin bitlerini ikili noktanın
/// sağına aynalamak (0.b0 b1 b2 ...). Üreteç matrisi birim matrisin tersidir.
uint32_t sobolDim0(uint32_t index) { return reverseBits(index); }

/// Sobol boyut 1. İlkel polinom x + 1, yön sayıları v_k = v_{k-1} ^ (v_{k-1} >> 1),
/// v_0 = 2^31. Bu, Pascal üçgeninin mod 2 hali olan üreteç matrisidir
/// (0x80000000, 0xC0000000, 0xA0000000, 0xF0000000, 0x88000000, ...).
/// Sonuç: indeksin 1 olan her biti k için v_k'lerin XOR'u.
uint32_t sobolDim1(uint32_t index) {
    uint32_t result = 0;
    for (uint32_t v = 0x80000000u; index != 0; index >>= 1, v ^= v >> 1) {
        if (index & 1u) result ^= v;
    }
    return result;
}

/// Laine–Karras permütasyonu (Burley 2020'nin iyileştirilmiş sabitleri). Çift sayılarla
/// çarpıp XOR'lamak, çıktının k. bitinin yalnızca girdinin ≤ k bitlerine bağlı olmasını
/// sağlar. Bit sırası ters çevrilmiş bir sayıda bu tam olarak Owen karıştırmasıdır:
/// her bit, kendinden daha "anlamlı" bitlerin belirlediği bir yazı-turaya göre çevrilir.
uint32_t laineKarras(uint32_t x, uint32_t seed) {
    x += seed;
    x ^= x * 0x6c50b47cu;
    x ^= x * 0xb82f1e52u;
    x ^= x * 0xc7afe638u;
    x ^= x * 0x8d22f6e6u;
    return x;
}

/// İç içe düzgün karıştırma (nested uniform scramble = Owen karıştırması):
/// ters çevir → Laine–Karras → geri ters çevir.
uint32_t nestedUniformScramble(uint32_t x, uint32_t seed) {
    return reverseBits(laineKarras(reverseBits(x), seed));
}

/// lowbias32 (C. Wellons): iyi çığ etkili 32-bit tamsayı karması.
uint32_t mix32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

uint32_t hashCombine(uint32_t h, uint32_t v) {
    return mix32(h ^ (v + 0x9e3779b9u + (h << 6) + (h >> 2)));
}

/// Üst 24 bit × 2^-24: float'ta tam temsil edilir ve en fazla 1 - 2^-24 olur (asla 1.0).
float toUnitFloat(uint32_t x) { return static_cast<float>(x >> 8) * 0x1p-24f; }

} // namespace

SobolSampler::SobolSampler(uint64_t seed) : m_seed(seed) {
    startPixel(0, 0);
}

/// Bir boyut için 2B nokta (Burley 2020, Listing 3 "shuffled_scrambled_sobol2d"):
///  1) Bu (piksel, boyut) için bağımsız bir tohum türet.
///  2) Örnek indeksini Owen-karıştır (shuffle). Karıştırma yüksek bitleri yalnızca
///     daha yüksek bitlere göre değiştirdiği için ilk 2^k indeks, Sobol dizisinin hizalı
///     bir 2^k'lık bloğuna gider → her 2'nin kuvveti ön-ek yine iyi tabakalanmış bir ağdır
///     ((0,2)-ağı). Farklı boyutlar farklı karıştırma aldığı için birbirinden bağımsızdır.
///  3) Sobol boyut 0 ve 1'i hesapla, her koordinatı kendi tohumuyla Owen-karıştır.
///     Owen karıştırması ağ özelliğini korur ama sonucu rastgele (yansız) yapar.
Vec2f SobolSampler::get2D() {
    uint32_t seed = hashCombine(m_pixelHash, m_dimension++);
    uint32_t index = nestedUniformScramble(m_index, seed);
    uint32_t x = nestedUniformScramble(sobolDim0(index), hashCombine(seed, 0x0u));
    uint32_t y = nestedUniformScramble(sobolDim1(index), hashCombine(seed, 0x1u));
    return {toUnitFloat(x), toUnitFloat(y)};
}

float SobolSampler::get1D() {
    // ponytail: 1B istek de bir 2B boyut harcar ve y'yi atar; 1B'ye özel yol gereksiz.
    return get2D().x;
}

std::unique_ptr<Sampler> SobolSampler::clone(uint64_t seed) const {
    return std::make_unique<SobolSampler>(seed);
}

void SobolSampler::startPixel(int x, int y) {
    uint32_t h = mix32(static_cast<uint32_t>(m_seed) ^ mix32(static_cast<uint32_t>(m_seed >> 32)));
    h = hashCombine(h, static_cast<uint32_t>(x));
    m_pixelHash = hashCombine(h, static_cast<uint32_t>(y));
    m_index = 0;
    m_dimension = 0;
}

void SobolSampler::startSample(int sampleIndex) {
    m_index = static_cast<uint32_t>(sampleIndex);
    m_dimension = 0;
}

std::unique_ptr<Sampler> createSobolSampler(uint64_t seed) {
    return std::make_unique<SobolSampler>(seed);
}

} // namespace photon
