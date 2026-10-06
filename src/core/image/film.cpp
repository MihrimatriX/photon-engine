// film.cpp — Film uygulaması: örnek ekleme (geçersizleri reddederek) ve ortalama çözümleme.
#include "core/image/film.h"
#include "core/math/float_bits.h"

#include <algorithm>
#include <cassert>

namespace photon {

Film::Film(int width, int height) {
    resize(width, height);
}

Film::Film(const Film& other)
    : m_width(other.m_width)
    , m_height(other.m_height)
    , m_sum(other.m_sum)
    , m_count(other.m_count)
    , m_rejected(other.rejectedSamples()) {}

Film& Film::operator=(const Film& other) {
    if (this != &other) {
        m_width = other.m_width;
        m_height = other.m_height;
        m_sum = other.m_sum;
        m_count = other.m_count;
        m_rejected.store(other.rejectedSamples(), std::memory_order_relaxed);
    }
    return *this;
}

void Film::resize(int width, int height) {
    m_width = std::max(0, width);
    m_height = std::max(0, height);
    const size_t n = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
    m_sum.assign(n * 3, 0.0f);
    m_count.assign(n, 0);
    m_rejected.store(0, std::memory_order_relaxed);
}

void Film::clear() {
    std::fill(m_sum.begin(), m_sum.end(), 0.0f);
    std::fill(m_count.begin(), m_count.end(), 0u);
    m_rejected.store(0, std::memory_order_relaxed);
}

// Piksel tahmini: I ≈ (1/N) Σ L_i. Geçersiz (NaN/Inf/negatif) örnek de N'e sayılır ama
// toplama 0 olarak girer — yani siyah bir örnek gibi davranır. Bu tahmini çok az aşağı
// çeker ama tek bir NaN'ın pikseli kalıcı olarak bozmasından iyidir; sayaç ile izlenir.
// Atomik değil: farklı iş parçacıkları farklı piksellere (karolara) yazdığı için güvenli.
void Film::addSample(int x, int y, const Color3f& L) {
    assert(x >= 0 && x < m_width && y >= 0 && y < m_height);
    const size_t flat = flatIndex(x, y);
    ++m_count[flat];
    const bool ok = finite3(L.r, L.g, L.b) && L.r >= 0.0f && L.g >= 0.0f && L.b >= 0.0f;
    if (!ok) {
        m_rejected.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    float* s = &m_sum[flat * 3];
    s[0] += L.r;
    s[1] += L.g;
    s[2] += L.b;
}

void Film::addSampleSigned(int x, int y, const Color3f& v) {
    assert(x >= 0 && x < m_width && y >= 0 && y < m_height);
    const size_t flat = flatIndex(x, y);
    ++m_count[flat];
    if (!finite3(v.r, v.g, v.b)) {
        m_rejected.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    float* s = &m_sum[flat * 3];
    s[0] += v.r;
    s[1] += v.g;
    s[2] += v.b;
}

Color3f Film::resolvedPixel(int x, int y) const {
    const size_t flat = flatIndex(x, y);
    const uint32_t n = m_count[flat];
    if (n == 0) return Color3f::black();
    const float inv = 1.0f / static_cast<float>(n);
    const float* s = &m_sum[flat * 3];
    return Color3f(s[0] * inv, s[1] * inv, s[2] * inv);
}

int Film::sampleCount(int x, int y) const {
    return static_cast<int>(m_count[flatIndex(x, y)]);
}

Image Film::resolve() const {
    Image out(m_width, m_height);
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            out.setPixel(x, y, resolvedPixel(x, y));
        }
    }
    return out;
}

} // namespace photon
