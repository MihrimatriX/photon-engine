// image.cpp — Image uygulaması: Float32 ve 8-bit kompakt LUT depolama, bilineer/trilineer
// sarmalı (wrap) örnekleme ve doğrusal ışıkta 2×2 mipmap piramidi.
#include "core/image/image.h"
#include "core/color/transfer.h"

#include <array>
#include <cmath>
#include <cstring>

namespace photon {

namespace {

const std::array<float, 256>& srgbToLinearLut() {
    static const std::array<float, 256> lut = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) t[static_cast<size_t>(i)] = srgbDecode(static_cast<float>(i) / 255.0f);
        return t;
    }();
    return lut;
}

const std::array<float, 256>& u8ToLinearLut() {
    static const std::array<float, 256> lut = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) t[static_cast<size_t>(i)] = static_cast<float>(i) / 255.0f;
        return t;
    }();
    return lut;
}

inline uint8_t encodeLinearToU8(float v, Image::StorageFormat fmt) {
    const float c = std::clamp(v, 0.0f, 1.0f);
    const float enc = fmt == Image::StorageFormat::U8Srgb ? srgbEncode(c) : c;
    return static_cast<uint8_t>(std::lround(enc * 255.0f));
}

inline int wrapIndex(int i, int n) {
    i %= n;
    return i < 0 ? i + n : i;
}

} // namespace

Image::Image(int width, int height) {
    resize(width, height);
}

Image Image::createCompact8(int width, int height, const uint8_t* rgb8, StorageFormat fmt) {
    Image img;
    if (width <= 0 || height <= 0) return img;
    if (fmt == StorageFormat::Float32) fmt = StorageFormat::U8Linear;
    img.m_width = width;
    img.m_height = height;
    img.m_format = fmt;
    const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
    img.m_dataU8.resize(bytes, 0);
    if (rgb8) std::memcpy(img.m_dataU8.data(), rgb8, bytes);
    return img;
}

void Image::resize(int width, int height) {
    m_width = std::max(0, width);
    m_height = std::max(0, height);
    m_format = StorageFormat::Float32;
    m_dataU8.clear();
    m_mipsF32.clear();
    m_mipsU8.clear();
    const size_t totalPixels = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
    m_data.assign(totalPixels * 3, 0.0f);
}

void Image::clear() {
    std::fill(m_data.begin(), m_data.end(), 0.0f);
    std::fill(m_dataU8.begin(), m_dataU8.end(), static_cast<uint8_t>(0));
    m_mipsF32.clear();
    m_mipsU8.clear();
}

Color3f Image::decodeU8(uint8_t r, uint8_t g, uint8_t b) const {
    const auto& lut = (m_format == StorageFormat::U8Srgb) ? srgbToLinearLut() : u8ToLinearLut();
    return Color3f(lut[r], lut[g], lut[b]);
}

void Image::setPixel(int x, int y, const Color3f& color) {
    const size_t idx = pixelIndex(x, y);
    if (m_format == StorageFormat::Float32) {
        m_data[idx + 0] = color.r;
        m_data[idx + 1] = color.g;
        m_data[idx + 2] = color.b;
    } else {
        m_dataU8[idx + 0] = encodeLinearToU8(color.r, m_format);
        m_dataU8[idx + 1] = encodeLinearToU8(color.g, m_format);
        m_dataU8[idx + 2] = encodeLinearToU8(color.b, m_format);
    }
}

Color3f Image::sampleBilinear(float u, float v) const {
    return sampleLevel(0, u, v);
}

void Image::buildMipmaps() {
    m_mipsF32.clear();
    m_mipsU8.clear();
    if (m_width <= 1 && m_height <= 1) return;

    int prevW = m_width;
    int prevH = m_height;
    // Doğrusal ışıkta 2×2 kutu ortalaması: yüksek seviyeler de doğru enerjiyi korur.
    std::vector<float> prevLin(static_cast<size_t>(prevW) * static_cast<size_t>(prevH) * 3);
    if (m_format == StorageFormat::Float32) {
        prevLin = m_data;
    } else {
        const auto& lut = (m_format == StorageFormat::U8Srgb) ? srgbToLinearLut() : u8ToLinearLut();
        for (size_t i = 0; i < prevLin.size(); ++i) prevLin[i] = lut[m_dataU8[i]];
    }

    while (prevW > 1 || prevH > 1) {
        const int nextW = std::max(1, prevW / 2);
        const int nextH = std::max(1, prevH / 2);
        std::vector<float> nextLin(static_cast<size_t>(nextW) * static_cast<size_t>(nextH) * 3, 0.0f);
        for (int y = 0; y < nextH; ++y) {
            const int y0 = std::min(y * 2, prevH - 1);
            const int y1 = std::min(y * 2 + 1, prevH - 1);
            for (int x = 0; x < nextW; ++x) {
                const int x0 = std::min(x * 2, prevW - 1);
                const int x1 = std::min(x * 2 + 1, prevW - 1);
                const size_t i00 = (static_cast<size_t>(y0) * static_cast<size_t>(prevW) + static_cast<size_t>(x0)) * 3;
                const size_t i10 = (static_cast<size_t>(y0) * static_cast<size_t>(prevW) + static_cast<size_t>(x1)) * 3;
                const size_t i01 = (static_cast<size_t>(y1) * static_cast<size_t>(prevW) + static_cast<size_t>(x0)) * 3;
                const size_t i11 = (static_cast<size_t>(y1) * static_cast<size_t>(prevW) + static_cast<size_t>(x1)) * 3;
                const size_t dst = (static_cast<size_t>(y) * static_cast<size_t>(nextW) + static_cast<size_t>(x)) * 3;
                for (int c = 0; c < 3; ++c) {
                    nextLin[dst + static_cast<size_t>(c)] =
                        0.25f * (prevLin[i00 + static_cast<size_t>(c)] + prevLin[i10 + static_cast<size_t>(c)] +
                                 prevLin[i01 + static_cast<size_t>(c)] + prevLin[i11 + static_cast<size_t>(c)]);
                }
            }
        }
        if (m_format == StorageFormat::Float32) {
            m_mipsF32.push_back(MipLevelF32{nextW, nextH, nextLin});
        } else {
            MipLevelU8 lvl;
            lvl.width = nextW;
            lvl.height = nextH;
            lvl.data.resize(nextLin.size());
            for (size_t i = 0; i < nextLin.size(); ++i) lvl.data[i] = encodeLinearToU8(nextLin[i], m_format);
            m_mipsU8.push_back(std::move(lvl));
        }
        prevLin = std::move(nextLin);
        prevW = nextW;
        prevH = nextH;
    }
}

int Image::mipLevels() const {
    if (m_width <= 0 || m_height <= 0) return 0;
    return 1 + static_cast<int>(m_format == StorageFormat::Float32 ? m_mipsF32.size() : m_mipsU8.size());
}

size_t Image::memoryBytes() const {
    size_t total = m_data.size() * sizeof(float) + m_dataU8.size() * sizeof(uint8_t);
    for (const auto& m : m_mipsF32) total += m.data.size() * sizeof(float);
    for (const auto& m : m_mipsU8) total += m.data.size() * sizeof(uint8_t);
    return total;
}

Color3f Image::sampleLevel(int level, float u, float v) const {
    if (m_width <= 0 || m_height <= 0) return Color3f::black();

    int w = m_width;
    int h = m_height;
    const float* f32 = nullptr;
    const uint8_t* u8 = nullptr;

    if (m_format == StorageFormat::Float32) {
        if (level <= 0 || m_mipsF32.empty()) {
            f32 = m_data.data();
        } else {
            const int idx = std::min(level - 1, static_cast<int>(m_mipsF32.size()) - 1);
            const auto& m = m_mipsF32[static_cast<size_t>(idx)];
            w = m.width;
            h = m.height;
            f32 = m.data.data();
        }
    } else {
        if (level <= 0 || m_mipsU8.empty()) {
            u8 = m_dataU8.data();
        } else {
            const int idx = std::min(level - 1, static_cast<int>(m_mipsU8.size()) - 1);
            const auto& m = m_mipsU8[static_cast<size_t>(idx)];
            w = m.width;
            h = m.height;
            u8 = m.data.data();
        }
    }

    u = u - std::floor(u);
    v = v - std::floor(v);
    const float x = u * static_cast<float>(w) - 0.5f;
    const float y = v * static_cast<float>(h) - 0.5f;
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);

    const int xa = wrapIndex(x0, w);
    const int x1 = wrapIndex(x0 + 1, w);
    const int ya = wrapIndex(y0, h);
    const int y1 = wrapIndex(y0 + 1, h);

    auto fetch = [&](int px, int py) -> Color3f {
        const size_t idx = (static_cast<size_t>(py) * static_cast<size_t>(w) + static_cast<size_t>(px)) * 3;
        if (f32) return Color3f(f32[idx + 0], f32[idx + 1], f32[idx + 2]);
        return decodeU8(u8[idx + 0], u8[idx + 1], u8[idx + 2]);
    };

    const Color3f c00 = fetch(xa, ya);
    const Color3f c10 = fetch(x1, ya);
    const Color3f c01 = fetch(xa, y1);
    const Color3f c11 = fetch(x1, y1);
    const Color3f c0 = c00 * (1.0f - fx) + c10 * fx;
    const Color3f c1 = c01 * (1.0f - fx) + c11 * fx;
    return c0 * (1.0f - fy) + c1 * fy;
}

Color3f Image::sampleMip(float u, float v, float lod) const {
    if (m_width <= 0 || m_height <= 0) return Color3f::black();
    if (!hasMipmaps() || lod <= 0.0f) return sampleLevel(0, u, v);
    const int maxLvl = mipLevels() - 1;
    const float clamped = std::clamp(lod, 0.0f, static_cast<float>(maxLvl));
    const int l0 = static_cast<int>(std::floor(clamped));
    const int l1 = std::min(l0 + 1, maxLvl);
    const float frac = clamped - static_cast<float>(l0);
    if (l0 == l1 || frac <= 1e-5f) return sampleLevel(l0, u, v);
    const Color3f c0 = sampleLevel(l0, u, v);
    const Color3f c1 = sampleLevel(l1, u, v);
    return c0 * (1.0f - frac) + c1 * frac;
}

} // namespace photon
