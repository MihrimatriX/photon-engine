// cryptomatte.cpp — Psyop Cryptomatte 1.2.0: MurmurHash3-32, güvenli float32 dönüşümü,
// kapsama sıralı rank çözümleyici ve JSON manifest üreticisi.
#include "core/image/cryptomatte.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace photon {

namespace {

inline uint32_t rotl32(uint32_t x, int8_t r) {
    return (x << r) | (x >> (32 - r));
}

std::string escapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(static_cast<char>(c));
        } else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
            out += buf;
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

} // namespace

uint32_t cryptoMurmurHash3_32(const void* key, size_t len, uint32_t seed) {
    const auto* data = static_cast<const uint8_t*>(key);
    const size_t nblocks = len / 4;
    uint32_t h1 = seed;
    constexpr uint32_t c1 = 0xcc9e2d51u;
    constexpr uint32_t c2 = 0x1b873593u;

    for (size_t i = 0; i < nblocks; ++i) {
        uint32_t k1 = 0;
        std::memcpy(&k1, data + i * 4, sizeof(uint32_t));
        k1 *= c1;
        k1 = rotl32(k1, 15);
        k1 *= c2;
        h1 ^= k1;
        h1 = rotl32(h1, 13);
        h1 = h1 * 5u + 0xe6546b64u;
    }

    const uint8_t* tail = data + nblocks * 4;
    uint32_t k1 = 0;
    switch (len & 3u) {
        case 3: k1 ^= static_cast<uint32_t>(tail[2]) << 16; [[fallthrough]];
        case 2: k1 ^= static_cast<uint32_t>(tail[1]) << 8;  [[fallthrough]];
        case 1:
            k1 ^= static_cast<uint32_t>(tail[0]);
            k1 *= c1;
            k1 = rotl32(k1, 15);
            k1 *= c2;
            h1 ^= k1;
            break;
        default: break;
    }

    h1 ^= static_cast<uint32_t>(len);
    h1 ^= h1 >> 16;
    h1 *= 0x85ebca6bu;
    h1 ^= h1 >> 13;
    h1 *= 0xc2b2ae35u;
    h1 ^= h1 >> 16;
    return h1;
}

uint32_t cryptoHashName(const std::string& name) {
    return cryptoMurmurHash3_32(name.data(), name.size(), 0u);
}

float cryptoHashToFloat(uint32_t hash) {
    // Psyop Cryptomatte 1.2.0: üs 0 (denormal/sıfır) ya da 255 (Inf/NaN) ise üssün en düşük
    // bitini çevir; böylece tüm kimlikler EXR 32-bit float'ta kayıpsız saklanır.
    const uint32_t m = hash << 9 >> 9;
    const uint32_t exp = (hash >> 23) & 0xFFu;
    uint32_t safe = hash;
    if (exp == 0u || exp == 0xFFu) {
        safe = (hash & 0x80000000u) | ((exp ^ 1u) << 23) | m;
    }
    float out = 0.0f;
    std::memcpy(&out, &safe, sizeof(float));
    return out;
}

float cryptoNameToFloat(const std::string& name) {
    return cryptoHashToFloat(cryptoHashName(name));
}

std::string cryptoHashHex(uint32_t hash) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08x", hash);
    return buf;
}

std::string cryptoMetadataPrefix(const std::string& layerName) {
    const std::string hex = cryptoHashHex(cryptoHashName(layerName));
    return "cryptomatte/" + hex.substr(0, 7) + "/";
}

CryptoFilm::CryptoFilm(int width, int height) {
    resize(width, height);
}

void CryptoFilm::resize(int width, int height) {
    m_width = std::max(0, width);
    m_height = std::max(0, height);
    m_pixels.assign(static_cast<size_t>(m_width) * static_cast<size_t>(m_height), PixelBucket{});
}

void CryptoFilm::clear() {
    for (PixelBucket& b : m_pixels) {
        b.total = 0;
        b.entries.clear();
    }
}

float CryptoFilm::registerName(const std::string& name) {
    if (name.empty()) return 0.0f;
    const uint32_t h = cryptoHashName(name);
    m_manifest[name] = h;
    return cryptoHashToFloat(h);
}

void CryptoFilm::addSample(int x, int y, float idHash) {
    if (x < 0 || x >= m_width || y < 0 || y >= m_height) return;
    PixelBucket& b = m_pixels[static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x)];
    ++b.total;
    if (idHash == 0.0f) return;
    for (PixelEntry& e : b.entries) {
        if (e.id == idHash) {
            ++e.count;
            return;
        }
    }
    b.entries.push_back(PixelEntry{idHash, 1u});
}

void CryptoFilm::addSamples(int x, int y, float idHash, uint32_t count) {
    if (count == 0 || x < 0 || x >= m_width || y < 0 || y >= m_height) return;
    PixelBucket& b = m_pixels[static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x)];
    if (idHash == 0.0f) return;
    for (PixelEntry& e : b.entries) {
        if (e.id == idHash) {
            e.count += count;
            return;
        }
    }
    b.entries.push_back(PixelEntry{idHash, count});
}

void CryptoFilm::addTotalSamples(int x, int y, uint32_t totalDelta) {
    if (totalDelta == 0 || x < 0 || x >= m_width || y < 0 || y >= m_height) return;
    PixelBucket& b = m_pixels[static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x)];
    b.total += totalDelta;
}

void CryptoFilm::merge(const CryptoFilm& other) {
    if (other.m_width != m_width || other.m_height != m_height) return;
    for (const auto& [name, h] : other.m_manifest) m_manifest[name] = h;
    for (size_t i = 0; i < m_pixels.size(); ++i) {
        const PixelBucket& src = other.m_pixels[i];
        if (src.total == 0) continue;
        PixelBucket& dst = m_pixels[i];
        dst.total += src.total;
        for (const PixelEntry& se : src.entries) {
            bool found = false;
            for (PixelEntry& de : dst.entries) {
                if (de.id == se.id) {
                    de.count += se.count;
                    found = true;
                    break;
                }
            }
            if (!found) dst.entries.push_back(se);
        }
    }
}

std::vector<std::pair<float, float>> CryptoFilm::pixelRanks(int x, int y) const {
    std::vector<std::pair<float, float>> out;
    if (x < 0 || x >= m_width || y < 0 || y >= m_height) return out;
    const PixelBucket& b = m_pixels[static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x)];
    if (b.total == 0 || b.entries.empty()) return out;
    std::vector<PixelEntry> sorted = b.entries;
    std::sort(sorted.begin(), sorted.end(), [](const PixelEntry& a, const PixelEntry& c) {
        return a.count > c.count;
    });
    const float inv = 1.0f / static_cast<float>(b.total);
    out.reserve(sorted.size());
    for (const PixelEntry& e : sorted) out.emplace_back(e.id, static_cast<float>(e.count) * inv);
    return out;
}

std::vector<CryptoRankImage> CryptoFilm::resolveRanks(int rankPairs) const {
    const int pairs = std::max(1, rankPairs);
    std::vector<CryptoRankImage> layers(static_cast<size_t>(pairs));
    const size_t n = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
    for (CryptoRankImage& img : layers) {
        img.width = m_width;
        img.height = m_height;
        img.rgba.assign(n * 4, 0.0f);
    }
    if (n == 0) return layers;

    std::vector<PixelEntry> tmp;
    for (size_t i = 0; i < n; ++i) {
        const PixelBucket& b = m_pixels[i];
        if (b.total == 0 || b.entries.empty()) continue;
        tmp = b.entries;
        std::sort(tmp.begin(), tmp.end(), [](const PixelEntry& a, const PixelEntry& c) {
            return a.count > c.count;
        });
        const float inv = 1.0f / static_cast<float>(b.total);
        for (int p = 0; p < pairs; ++p) {
            const size_t r0 = static_cast<size_t>(p * 2);
            const size_t r1 = r0 + 1;
            float* dst = &layers[static_cast<size_t>(p)].rgba[i * 4];
            if (r0 < tmp.size()) {
                dst[0] = tmp[r0].id;
                dst[1] = static_cast<float>(tmp[r0].count) * inv;
            }
            if (r1 < tmp.size()) {
                dst[2] = tmp[r1].id;
                dst[3] = static_cast<float>(tmp[r1].count) * inv;
            }
        }
    }
    return layers;
}

std::string CryptoFilm::manifestJson() const {
    std::vector<std::pair<std::string, uint32_t>> sorted(m_manifest.begin(), m_manifest.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::string out = "{";
    for (size_t i = 0; i < sorted.size(); ++i) {
        if (i > 0) out.push_back(',');
        out.push_back('"');
        out += escapeJson(sorted[i].first);
        out += "\":\"";
        out += cryptoHashHex(sorted[i].second);
        out.push_back('"');
    }
    out.push_back('}');
    return out;
}

std::vector<std::pair<std::string, std::string>> CryptoFilm::exrMetadata(const std::string& layerName) const {
    const std::string p = cryptoMetadataPrefix(layerName);
    return {
        {p + "name", layerName},
        {p + "hash", "MurmurHash3_32"},
        {p + "conversion", "uint32_to_float32"},
        {p + "manifest", manifestJson()},
    };
}

} // namespace photon
