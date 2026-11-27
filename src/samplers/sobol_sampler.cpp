// sobol_sampler.cpp — Burley 2020 hash tabanlı Owen karıştırmalı Sobol örnekleyicisinin uygulaması.
// Yalnızca Sobol'ün ilk iki boyutu kullanılır (bit ters çevirme + Pascal üçgeni
// üreteci); daha yüksek boyutlar her çağrıda bağımsız karıştırma ile "dolgu" yapılır.

#include "samplers/sobol_sampler.h"

namespace photon {

namespace {

/// 32 bitin sırasını ters çevirir (bit 0 ↔ bit 31, ...). Böl-ve-yönet: baytların sırası
/// ters çevrilir (bswap), sonra her bayt içinde 4'lü, 2'li ve komşu bit grupları yer değiştirir.
/// Grup takasları birbirinden bağımsız olduğu için sıra sonucu değiştirmez.
uint32_t reverseBits(uint32_t x) {
#if defined(__GNUC__) || defined(__clang__)
    x = __builtin_bswap32(x); // 8'li ve 16'lı grupların takası tek komutta
#else
    x = ((x >> 8) & 0x00FF00FFu) | ((x & 0x00FF00FFu) << 8);
    x = (x >> 16) | (x << 16);
#endif
    x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
    x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
    x = ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);
    return x;
}

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

/// reverseBits(sobolDim1(i)) bayt tablolarıyla. Sobol üreteci GF(2) üzerinde doğrusaldır
/// (s(a ^ b) = s(a) ^ s(b)), bit ters çevirme de öyle; bu yüzden 32 bitlik indeks dört bayta
/// bölünür ve her baytın katkısı önceden hesaplanmış tablodan XOR'lanır. Karıştırılmış indeks
/// rastgele 32 bit olduğu için döngü her çağrıda ~32 tur ve tahmin edilemez dallanma demekti
/// (profilde örnekleyici render süresinin ~%28'iydi).
struct ReversedSobolDim1Table {
    uint32_t t[4][256];
    ReversedSobolDim1Table() {
        for (int k = 0; k < 4; ++k)
            for (uint32_t b = 0; b < 256; ++b) t[k][b] = reverseBits(sobolDim1(b << (8 * k)));
    }
};

inline uint32_t reversedSobolDim1(uint32_t index) {
    static const ReversedSobolDim1Table tab;
    return tab.t[0][index & 0xFFu] ^ tab.t[1][(index >> 8) & 0xFFu] ^ tab.t[2][(index >> 16) & 0xFFu] ^
           tab.t[3][index >> 24];
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

/// lowbias32 (C. Wellons): iyi çığ etkili 32-bit tamsayı karması.
uint32_t mix32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

/// Mevcut karmaya yeni bir değer katar (boost::hash_combine kalıbı; 0x9e3779b9 = 2^32/φ,
/// altın oran sabiti). Sonuç mix32'den geçirilir ki benzer girdiler ilişkisiz tohum versin.
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
///
/// Uygulama, iç içe düzgün karıştırmanın tanımını (NUS(x) = rev(LK(rev(x)))) açarak gereksiz
/// bit ters çevirmelerini sadeleştirir; sonuç tanımla bit bit aynıdır (test_sobol.cpp
/// MatchesReferenceDefinitionBitExact):
///   index = NUS(m_index)          = rev(LK(rev(m_index), seed))    — rev(m_index) örnek başına bir kez
///   x     = NUS(sobol0(index))    = rev(LK(index, s0))             — sobol0 = rev, rev∘rev = birim
///   y     = NUS(sobol1(index))    = rev(LK(rev(sobol1(index)), s1)) — rev∘sobol1 bayt tablolarından
Vec2f SobolSampler::get2D() {
    const uint32_t seed = hashCombine(m_pixelHash, m_dimension++);
    const uint32_t index = reverseBits(laineKarras(m_revIndex, seed));
    const uint32_t x = reverseBits(laineKarras(index, hashCombine(seed, 0x0u)));
    const uint32_t y = reverseBits(laineKarras(reversedSobolDim1(index), hashCombine(seed, 0x1u)));
    return {toUnitFloat(x), toUnitFloat(y)};
}

float SobolSampler::get1D() {
    // 1B istek de bir 2B boyut harcar (boyut numaralaması değişmesin) ama y hiç hesaplanmaz:
    // değer get2D().x ile bit bit aynıdır.
    const uint32_t seed = hashCombine(m_pixelHash, m_dimension++);
    const uint32_t index = reverseBits(laineKarras(m_revIndex, seed));
    return toUnitFloat(reverseBits(laineKarras(index, hashCombine(seed, 0x0u))));
}

std::unique_ptr<Sampler> SobolSampler::clone(uint64_t seed) const {
    return std::make_unique<SobolSampler>(seed);
}

// Piksel karması = karma(tohum, x, y): her piksel kendi karıştırmasını alır. Komşu piksellerin
// hataları ilişkisiz olur ve yapısal desen (moiré) yerine ince gürültü görünür.
void SobolSampler::startPixel(int x, int y) {
    uint32_t h = mix32(static_cast<uint32_t>(m_seed) ^ mix32(static_cast<uint32_t>(m_seed >> 32)));
    h = hashCombine(h, static_cast<uint32_t>(x));
    m_pixelHash = hashCombine(h, static_cast<uint32_t>(y));
    m_index = 0;
    m_revIndex = 0;
    m_dimension = 0;
}

void SobolSampler::startSample(int sampleIndex) {
    m_index = static_cast<uint32_t>(sampleIndex);
    m_revIndex = reverseBits(m_index);
    m_dimension = 0;
}

std::unique_ptr<Sampler> createSobolSampler(uint64_t seed) {
    return std::make_unique<SobolSampler>(seed);
}

} // namespace photon
