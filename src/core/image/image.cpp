// image.cpp — Image sınıfının uygulaması; en önemli kısmı sarmalayan (wrap) bilineer örnekleme.
#include "image.h"
#include <cmath>

namespace photon {

Image::Image(int width, int height) {
    resize(width, height);
}

void Image::resize(int width, int height) {
    m_width = width;
    m_height = height;

    const size_t totalPixels = static_cast<size_t>(width) * height;
    m_data.assign(totalPixels * 3, 0.0f);
}

void Image::clear() {
    std::fill(m_data.begin(), m_data.end(), 0.0f);
}

void Image::setPixel(int x, int y, const Color3f& color) {
    const size_t idx = pixelIndex(x, y);
    m_data[idx + 0] = color.r;
    m_data[idx + 1] = color.g;
    m_data[idx + 2] = color.b;
}

Color3f Image::getPixel(int x, int y) const {
    const size_t idx = pixelIndex(x, y);
    return Color3f(m_data[idx + 0], m_data[idx + 1], m_data[idx + 2]);
}

// Bilineer filtreleme: (u,v)'yi piksel koordinatına çevir, çevredeki 2×2 pikseli bul ve
// önce x'te, sonra y'de doğrusal ara değer al: c = lerp(lerp(c00,c10,fx), lerp(c01,c11,fx), fy).
// −0.5 kaydırması: piksel merkezleri tamsayı+0.5'tedir (piksel i, [i, i+1) aralığını kaplar);
// böylece tam bir piksel merkezine bakan UV o pikselin değerini aynen döndürür.
// Kenarlarda indeksler modülo ile sarılır (tekrarlanan doku / ortam haritasında φ dikişi).
Color3f Image::sampleBilinear(float u, float v) const {
    if (m_width <= 0 || m_height <= 0) return Color3f::black();

    // Wrap UV
    u = u - std::floor(u);
    v = v - std::floor(v);

    const float x = u * static_cast<float>(m_width) - 0.5f;
    const float y = v * static_cast<float>(m_height) - 0.5f;
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);

    auto wrap = [](int i, int n) {
        i %= n;
        return i < 0 ? i + n : i;
    };
    const int x1 = wrap(x0 + 1, m_width);
    const int y1 = wrap(y0 + 1, m_height);
    const int xa = wrap(x0, m_width);
    const int ya = wrap(y0, m_height);

    const Color3f c00 = getPixel(xa, ya);
    const Color3f c10 = getPixel(x1, ya);
    const Color3f c01 = getPixel(xa, y1);
    const Color3f c11 = getPixel(x1, y1);
    const Color3f c0 = c00 * (1.0f - fx) + c10 * fx;
    const Color3f c1 = c01 * (1.0f - fx) + c11 * fx;
    return c0 * (1.0f - fy) + c1 * fy;
}

} // namespace photon
