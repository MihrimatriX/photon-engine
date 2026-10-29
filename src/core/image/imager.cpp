// imager.cpp — Bloom (çok ölçekli Gauss), yıldız parlaması, Bradford beyaz dengesi,
// log-kontrast, doygunluk, vinyet ve yanal renk sapması.
#include "core/image/imager.h"
#include "core/math/constants.h"

#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>

namespace photon {

namespace {

/// Satırları iş parçacıklarına böler (render havuzundan bağımsız; çağıran bir havuz
/// görevi olabilir).
template <typename F>
void parallelRows(int h, F&& fn) {
    const int n = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 16);
    if (h < 64 || n == 1) {
        fn(0, h);
        return;
    }
    std::vector<std::thread> ts;
    for (int t = 0; t < n; ++t) ts.emplace_back([&, t] { fn(h * t / n, h * (t + 1) / n); });
    for (auto& t : ts) t.join();
}

Color3f sampleClamped(const Image& img, float x, float y) {
    const int w = img.width(), h = img.height();
    x = std::clamp(x - 0.5f, 0.0f, static_cast<float>(w - 1));
    y = std::clamp(y - 0.5f, 0.0f, static_cast<float>(h - 1));
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    const float fx = x - static_cast<float>(x0), fy = y - static_cast<float>(y0);
    const Color3f a = img.getPixel(x0, y0) * (1.0f - fx) + img.getPixel(x1, y0) * fx;
    const Color3f b = img.getPixel(x0, y1) * (1.0f - fx) + img.getPixel(x1, y1) * fx;
    return a * (1.0f - fy) + b * fy;
}

Image downsample2(const Image& src) {
    const int w = std::max(1, src.width() / 2), h = std::max(1, src.height() / 2);
    Image out(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int sx = std::min(2 * x, src.width() - 1), sy = std::min(2 * y, src.height() - 1);
            const int sx1 = std::min(sx + 1, src.width() - 1), sy1 = std::min(sy + 1, src.height() - 1);
            out.setPixel(x, y, (src.getPixel(sx, sy) + src.getPixel(sx1, sy) + src.getPixel(sx, sy1) +
                                src.getPixel(sx1, sy1)) * 0.25f);
        }
    return out;
}

/// Ayrılabilir Gauss bulanıklığı (σ piksel), kenarlar kenetlenir.
void gaussianBlur(Image& img, float sigma) {
    if (sigma < 0.3f) return;
    const int r = std::max(1, static_cast<int>(std::ceil(3.0f * sigma)));
    std::vector<float> k(static_cast<size_t>(2 * r + 1));
    float sum = 0.0f;
    for (int i = -r; i <= r; ++i) sum += k[static_cast<size_t>(i + r)] = std::exp(-0.5f * (i * i) / (sigma * sigma));
    for (float& v : k) v /= sum;
    const int w = img.width(), h = img.height();
    Image tmp(w, h);
    parallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < w; ++x) {
                Color3f acc;
                for (int i = -r; i <= r; ++i) acc += img.getPixel(std::clamp(x + i, 0, w - 1), y) * k[static_cast<size_t>(i + r)];
                tmp.setPixel(x, y, acc);
            }
    });
    parallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < w; ++x) {
                Color3f acc;
                for (int i = -r; i <= r; ++i) acc += tmp.getPixel(x, std::clamp(y + i, 0, h - 1)) * k[static_cast<size_t>(i + r)];
                img.setPixel(x, y, acc);
            }
    });
}

/// Eşik üstü parlaklık (renk korunur), yarım çözünürlükte.
Image brightPass(const Image& hdr, float threshold, float exposureScale) {
    Image half = downsample2(hdr);
    for (int y = 0; y < half.height(); ++y)
        for (int x = 0; x < half.width(); ++x) {
            const Color3f c = half.getPixel(x, y);
            const float L = c.luminance() * exposureScale;
            const float f = L > threshold ? (L - threshold) / L : 0.0f;
            half.setPixel(x, y, c * f);
        }
    return half;
}

/// Bloom: üç oktav Gauss (σ, 2σ, 4σ), büyük σ'lar küçültülmüş görüntüde (hız).
Image bloomImage(const Image& bright, float sigmaHalfRes) {
    Image accum(bright.width(), bright.height());
    const float weights[3] = {0.5f, 0.3f, 0.2f};
    for (int o = 0; o < 3; ++o) {
        float sigma = sigmaHalfRes * static_cast<float>(1 << o);
        Image level = bright;
        int scale = 1;
        while (sigma > 6.0f && level.width() > 8 && level.height() > 8) {
            level = downsample2(level);
            sigma *= 0.5f;
            scale *= 2;
        }
        gaussianBlur(level, sigma);
        for (int y = 0; y < accum.height(); ++y)
            for (int x = 0; x < accum.width(); ++x) {
                const Color3f c = scale == 1 ? level.getPixel(x, y)
                                             : sampleClamped(level, (x + 0.5f) / scale, (y + 0.5f) / scale);
                accum.setPixel(x, y, accum.getPixel(x, y) + c * weights[o]);
            }
    }
    return accum;
}

/// Yıldız parlaması: her ışın doğrultusunda üstel azalan ağırlıklı çizgi evrişimi.
Image glareImage(const Image& bright, int blades, float angleDeg, float lengthPx) {
    Image src = downsample2(bright); // çeyrek çözünürlük
    const float L = std::max(2.0f, lengthPx * 0.25f);
    const int lines = std::max(1, (blades + 1) / 2);
    const int steps = static_cast<int>(std::ceil(L));
    Image out(src.width(), src.height());
    std::vector<float> wts(static_cast<size_t>(steps + 1));
    float wsum = 0.0f;
    for (int t = 0; t <= steps; ++t) {
        wts[static_cast<size_t>(t)] = std::exp(-3.0f * static_cast<float>(t) / L);
        wsum += (t == 0 ? 1.0f : 2.0f) * wts[static_cast<size_t>(t)];
    }
    for (int l = 0; l < lines; ++l) {
        const float a = angleDeg * DEG_TO_RAD + static_cast<float>(l) * PI / static_cast<float>(lines);
        const float dx = std::cos(a), dy = std::sin(a);
        parallelRows(src.height(), [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y)
                for (int x = 0; x < src.width(); ++x) {
                    Color3f acc = src.getPixel(x, y) * wts[0];
                    for (int t = 1; t <= steps; ++t) {
                        const float w = wts[static_cast<size_t>(t)];
                        acc += sampleClamped(src, x + 0.5f + dx * t, y + 0.5f + dy * t) * w;
                        acc += sampleClamped(src, x + 0.5f - dx * t, y + 0.5f - dy * t) * w;
                    }
                    out.setPixel(x, y, out.getPixel(x, y) + acc / (wsum * static_cast<float>(lines)));
                }
        });
    }
    return out;
}

// CIE XYZ ↔ doğrusal sRGB (D65).
constexpr float kRgbToXyz[9] = {0.4124564f, 0.3575761f, 0.1804375f, 0.2126729f, 0.7151522f,
                                0.0721750f, 0.0193339f, 0.1191920f, 0.9503041f};
constexpr float kXyzToRgb[9] = {3.2404542f, -1.5371385f, -0.4985314f, -0.9692660f, 1.8760108f,
                                0.0415560f, 0.0556434f, -0.2040259f, 1.0572252f};
constexpr float kBradford[9] = {0.8951f, 0.2664f, -0.1614f, -0.7502f, 1.7135f, 0.0367f, 0.0389f, -0.0685f, 1.0296f};
constexpr float kBradfordInv[9] = {0.9869929f, -0.1470543f, 0.1599627f, 0.4323053f, 0.5183603f,
                                   0.0492912f, -0.0085287f, 0.0400428f, 0.9684867f};

void mul3(const float a[9], const float b[9], float out[9]) {
    float r[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
    std::copy(r, r + 9, out);
}

/// Planck eğrisi üzerinde T'nin kromatiklik koordinatları (Kim vd. 2002 kübik yaklaşımı).
void planckXy(float T, float& x, float& y) {
    T = std::clamp(T, 1667.0f, 25000.0f);
    const double t = T, t2 = t * t, t3 = t2 * t;
    double xx = T <= 4000.0f ? -0.2661239e9 / t3 - 0.2343589e6 / t2 + 0.8776956e3 / t + 0.179910
                             : -3.0258469e9 / t3 + 2.1070379e6 / t2 + 0.2226347e3 / t + 0.240390;
    double yy;
    if (T <= 2222.0f) yy = -1.1063814 * xx * xx * xx - 1.34811020 * xx * xx + 2.18555832 * xx - 0.20219683;
    else if (T <= 4000.0f) yy = -0.9549476 * xx * xx * xx - 1.37418593 * xx * xx + 2.09137015 * xx - 0.16748867;
    else yy = 3.0817580 * xx * xx * xx - 5.87338670 * xx * xx + 3.75112997 * xx - 0.37001483;
    x = static_cast<float>(xx);
    y = static_cast<float>(yy);
}

/// CIE gün ışığı eğrisi (4000–25000 K). 6504 K'de D65'e denk gelir.
void daylightXy(float T, float& x, float& y) {
    T = std::clamp(T, 4000.0f, 25000.0f);
    const double t = T, t2 = t * t, t3 = t2 * t;
    const double xx = T <= 7000.0f ? -4.6070e9 / t3 + 2.9678e6 / t2 + 0.09911e3 / t + 0.244063
                                   : -2.0064e9 / t3 + 1.9018e6 / t2 + 0.24748e3 / t + 0.237040;
    x = static_cast<float>(xx);
    y = static_cast<float>(-3.0 * xx * xx + 2.870 * xx - 0.275);
}

/// Işık kaynağı beyazı: düşük sıcaklıkta akkor (Planck), yüksekte gün ışığı; 3500–4500 K
/// arasında yumuşak geçiş (iki eğri 4000 K'de birkaç binde bir ayrışır).
void illuminantXy(float T, float& x, float& y) {
    float xp, yp, xd, yd;
    planckXy(T, xp, yp);
    daylightXy(T, xd, yd);
    const float t = std::clamp((T - 3500.0f) / 1000.0f, 0.0f, 1.0f);
    x = xp + (xd - xp) * t;
    y = yp + (yd - yp) * t;
}

} // namespace

Color3f temperatureColor(float kelvin) {
    float x, y;
    illuminantXy(std::clamp(kelvin, 1000.0f, 25000.0f), x, y);
    const float X = x / y, Y = 1.0f, Z = (1.0f - x - y) / y;
    float rgb[3];
    for (int i = 0; i < 3; ++i) rgb[i] = std::max(0.0f, kXyzToRgb[i * 3] * X + kXyzToRgb[i * 3 + 1] * Y + kXyzToRgb[i * 3 + 2] * Z);
    Color3f c(rgb[0], rgb[1], rgb[2]);
    const float lum = c.luminance();
    return lum > 0.0f ? c / lum : Color3f(1.0f);
}

void whiteBalanceMatrix(float temperatureK, float tint, float out[9]) {
    float xs, ys;
    illuminantXy(temperatureK, xs, ys);
    const float xd = 0.31271f, yd = 0.32902f; // D65: sRGB'nin beyazı
    ys += tint * 0.02f; // macenta (+) / yeşil (−) ekseni
    // Kaynak ve hedef beyazların XYZ'si → Bradford konileri → oran (von Kries).
    const float src[3] = {xs / ys, 1.0f, (1.0f - xs - ys) / ys};
    const float dst[3] = {xd / yd, 1.0f, (1.0f - xd - yd) / yd};
    float lmsS[3], lmsD[3];
    for (int i = 0; i < 3; ++i) {
        lmsS[i] = kBradford[i * 3] * src[0] + kBradford[i * 3 + 1] * src[1] + kBradford[i * 3 + 2] * src[2];
        lmsD[i] = kBradford[i * 3] * dst[0] + kBradford[i * 3 + 1] * dst[1] + kBradford[i * 3 + 2] * dst[2];
    }
    const float D[9] = {lmsD[0] / lmsS[0], 0, 0, 0, lmsD[1] / lmsS[1], 0, 0, 0, lmsD[2] / lmsS[2]};
    float m[9];
    mul3(D, kBradford, m);
    mul3(kBradfordInv, m, m);
    mul3(m, kRgbToXyz, m);
    mul3(kXyzToRgb, m, out);
}

void applyImager(Image& hdr, const ImagerSettings& s, float exposureEV) {
    if (s.isIdentity() || hdr.width() <= 0 || hdr.height() <= 0) return;
    const int w = hdr.width(), h = hdr.height();
    const float diag = std::sqrt(static_cast<float>(w) * w + static_cast<float>(h) * h);
    const float cx = 0.5f * w, cy = 0.5f * h;
    const float exposureScale = std::exp2(exposureEV);

    // 1) Yanal renk sapması: kırmızı dışa, mavi içe ölçeklenir (merkezde sıfır).
    if (s.chromaticAberration > 0.0f) {
        const Image src = hdr;
        const float k = s.chromaticAberration * 1e-3f * diag / (0.5f * diag);
        parallelRows(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y)
                for (int x = 0; x < w; ++x) {
                    const float px = x + 0.5f - cx, py = y + 0.5f - cy;
                    const Color3f r = sampleClamped(src, cx + px * (1.0f + k), cy + py * (1.0f + k));
                    const Color3f b = sampleClamped(src, cx + px * (1.0f - k), cy + py * (1.0f - k));
                    hdr.setPixel(x, y, Color3f(r.r, src.getPixel(x, y).g, b.b));
                }
        });
    }

    // 2–3) Bloom ve glare: eşik üstü ışık yarım çözünürlükte işlenip geri eklenir.
    if (s.bloomIntensity > 0.0f || s.glareIntensity > 0.0f) {
        const Image bright = brightPass(hdr, std::max(0.0f, s.bloomThreshold), exposureScale);
        Image bloom, glare;
        if (s.bloomIntensity > 0.0f) bloom = bloomImage(bright, std::max(0.5f, s.bloomRadius * diag * 0.5f * 0.5f));
        if (s.glareIntensity > 0.0f) glare = glareImage(bright, std::max(2, s.glareBlades), s.glareAngleDeg,
                                                        std::max(4.0f, s.glareLength * diag));
        parallelRows(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y)
                for (int x = 0; x < w; ++x) {
                    Color3f c = hdr.getPixel(x, y);
                    if (s.bloomIntensity > 0.0f) c += sampleClamped(bloom, (x + 0.5f) * 0.5f, (y + 0.5f) * 0.5f) * s.bloomIntensity;
                    if (s.glareIntensity > 0.0f)
                        c += sampleClamped(glare, (x + 0.5f) * 0.25f, (y + 0.5f) * 0.25f) * (s.glareIntensity * 4.0f);
                    hdr.setPixel(x, y, c);
                }
        });
    }

    // 4–6) Piksel başına renk işlemleri.
    float wb[9];
    const bool doWb = s.whiteBalanceK != 6500.0f || s.tint != 0.0f;
    if (doWb) whiteBalanceMatrix(s.whiteBalanceK, s.tint, wb);
    const float halfDiag = 0.5f * diag;
    parallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < w; ++x) {
                Color3f c = hdr.getPixel(x, y);
                if (doWb) {
                    c = Color3f(wb[0] * c.r + wb[1] * c.g + wb[2] * c.b, wb[3] * c.r + wb[4] * c.g + wb[5] * c.b,
                                wb[6] * c.r + wb[7] * c.g + wb[8] * c.b);
                }
                if (s.contrast != 1.0f || s.saturation != 1.0f) {
                    const float L = std::max(c.luminance(), 0.0f);
                    float L2 = L;
                    if (s.contrast != 1.0f && L > 0.0f) {
                        // Pozlanmış orta gri (0.18) etrafında güç eğrisi: gölgeler ve ışıklar ayrışır.
                        const float Le = L * exposureScale;
                        L2 = 0.18f * std::pow(Le / 0.18f, s.contrast) / exposureScale;
                        c *= L2 / L;
                    }
                    if (s.saturation != 1.0f) c = Color3f(L2) + (c - Color3f(L2)) * s.saturation;
                }
                if (s.vignette > 0.0f) {
                    const float dx = (x + 0.5f - cx) / halfDiag, dy = (y + 0.5f - cy) / halfDiag;
                    const float r2 = dx * dx + dy * dy;
                    c *= std::max(0.0f, 1.0f - s.vignette * r2 * r2 * 0.9f - s.vignette * r2 * 0.1f);
                }
                hdr.setPixel(x, y, Color3f(std::max(0.0f, c.r), std::max(0.0f, c.g), std::max(0.0f, c.b)));
            }
    });
}

} // namespace photon
