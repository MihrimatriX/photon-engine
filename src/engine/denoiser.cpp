#include "engine/denoiser.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#if defined(PHOTON_ENABLE_OIDN)
#include <OpenImageDenoise/oidn.hpp>
#endif

namespace photon {

namespace {

bool softBlurDenoise(Image& color) {
    const int w = color.width();
    const int h = color.height();
    if (w <= 0 || h <= 0) return false;

    // 3×3 box on averaged HDR — reduces fireflies when OIDN SDK is absent.
    std::vector<Color3f> src(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            src[static_cast<size_t>(y) * w + x] = color.getPixel(x, y);
        }
    }

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Color3f sum(0.0f);
            float wsum = 0.0f;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = std::clamp(x + dx, 0, w - 1);
                    int ny = std::clamp(y + dy, 0, h - 1);
                    Color3f c = src[static_cast<size_t>(ny) * w + nx];
                    float lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
                    // Down-weight extreme fireflies
                    float weight = 1.0f / (1.0f + lum * lum);
                    sum += c * weight;
                    wsum += weight;
                }
            }
            color.setPixel(x, y, sum / std::max(wsum, 1e-6f));
        }
    }
    return true;
}

} // namespace

Image denoiseCopy(const Image& color) {
    Image out = color;
    denoiseImage(out);
    return out;
}

bool denoiseAvailable() {
#if defined(PHOTON_ENABLE_OIDN)
    return true;
#else
    return false;
#endif
}

bool denoiseImage(Image& color, const Image* albedo, const Image* normal) {
#if !defined(PHOTON_ENABLE_OIDN)
    (void)albedo;
    (void)normal;
    return softBlurDenoise(color);
#else
    const int w = color.width();
    const int h = color.height();
    if (w <= 0 || h <= 0) return false;

    std::vector<float> beauty(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Color3f c = color.getPixel(x, y);
            const size_t i = (static_cast<size_t>(y) * w + x) * 3;
            beauty[i + 0] = c.r;
            beauty[i + 1] = c.g;
            beauty[i + 2] = c.b;
        }
    }

    try {
        oidn::DeviceRef device = oidn::newDevice();
        device.commit();

        oidn::FilterRef filter = device.newFilter("RT");
        filter.setImage("color", beauty.data(), oidn::Format::Float3, w, h);
        filter.setImage("output", beauty.data(), oidn::Format::Float3, w, h);

        std::vector<float> albedoBuf, normalBuf;
        if (albedo && albedo->width() == w && albedo->height() == h) {
            albedoBuf.resize(beauty.size());
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    Color3f c = albedo->getPixel(x, y);
                    const size_t i = (static_cast<size_t>(y) * w + x) * 3;
                    albedoBuf[i + 0] = c.r;
                    albedoBuf[i + 1] = c.g;
                    albedoBuf[i + 2] = c.b;
                }
            }
            filter.setImage("albedo", albedoBuf.data(), oidn::Format::Float3, w, h);
        }
        if (normal && normal->width() == w && normal->height() == h) {
            normalBuf.resize(beauty.size());
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    Color3f c = normal->getPixel(x, y);
                    const size_t i = (static_cast<size_t>(y) * w + x) * 3;
                    normalBuf[i + 0] = c.r;
                    normalBuf[i + 1] = c.g;
                    normalBuf[i + 2] = c.b;
                }
            }
            filter.setImage("normal", normalBuf.data(), oidn::Format::Float3, w, h);
        }

        filter.set("hdr", true);
        filter.commit();
        filter.execute();

        const char* errorMessage = nullptr;
        if (device.getError(errorMessage) != oidn::Error::None) {
            std::cerr << "OIDN error: " << (errorMessage ? errorMessage : "unknown")
                      << " — falling back to soft blur" << std::endl;
            return softBlurDenoise(color);
        }

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const size_t i = (static_cast<size_t>(y) * w + x) * 3;
                color.setPixel(x, y, Color3f(beauty[i], beauty[i + 1], beauty[i + 2]));
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "OIDN exception: " << e.what() << " — soft blur fallback" << std::endl;
        return softBlurDenoise(color);
    }
#endif
}

} // namespace photon
