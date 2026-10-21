// denoiser.cpp — OIDN sarmalayıcısı ve OIDN yoksa kullanılan yumuşatma yedeği.
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

// OIDN yokken: 3×3 komşulukta parlaklığa ters ağırlıklı ortalama. Ateş böceği
// (firefly) denilen tek parlak pikselleri bastırır ama ayrıntıyı da bulanıklaştırır.
bool softBlurDenoise(Image& color) {
    const int w = color.width();
    const int h = color.height();
    if (w <= 0 || h <= 0) return false;

    Image src = color;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Color3f sum(0.0f);
            float wsum = 0.0f;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = std::clamp(x + dx, 0, w - 1);
                    int ny = std::clamp(y + dy, 0, h - 1);
                    Color3f c = src.getPixel(nx, ny);
                    float lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
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

#if defined(PHOTON_ENABLE_OIDN)
struct Denoiser::Impl {
    oidn::DeviceRef device;
    bool ok = false;
    Impl() {
        try {
            // GTX 1080 (Pascal) OIDN GPU modunu desteklemez; CPU cihazı seçilir.
            device = oidn::newDevice(oidn::DeviceType::CPU);
            device.commit();
            const char* msg = nullptr;
            ok = device.getError(msg) == oidn::Error::None;
            if (!ok) std::cerr << "OIDN device error: " << (msg ? msg : "?") << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "OIDN init failed: " << e.what() << std::endl;
        }
    }
};
#else
struct Denoiser::Impl {};
#endif

Denoiser::Denoiser() : m_impl(std::make_unique<Impl>()) {}
Denoiser::~Denoiser() = default;

bool Denoiser::run(Image& color, const Image* albedo, const Image* normal, Quality quality) {
    std::lock_guard<std::mutex> lock(m_mutex);
#if !defined(PHOTON_ENABLE_OIDN)
    (void)albedo;
    (void)normal;
    (void)quality;
    softBlurDenoise(color);
    return false;
#else
    const int w = color.width();
    const int h = color.height();
    if (w <= 0 || h <= 0) return false;
    if (!m_impl->ok) {
        softBlurDenoise(color);
        return false;
    }

    try {
        // Image zaten sıkı paketlenmiş float RGB; OIDN doğrudan onun belleğini okur.
        // Çıktı ayrı bir tampona yazılır (yerinde çalıştırmak OIDN'de desteklenir
        // ama ayrı tampon, hata durumunda girdiyi bozmadan yedeğe düşmeyi sağlar).
        std::vector<float> outBuf(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
        oidn::FilterRef filter = m_impl->device.newFilter("RT");
        filter.setImage("color", color.data(), oidn::Format::Float3, w, h);
        filter.setImage("output", outBuf.data(), oidn::Format::Float3, w, h);
        const bool hasAlbedo = albedo && albedo->width() == w && albedo->height() == h;
        const bool hasNormal = hasAlbedo && normal && normal->width() == w && normal->height() == h;
        if (hasAlbedo) filter.setImage("albedo", const_cast<float*>(albedo->data()), oidn::Format::Float3, w, h);
        // OIDN normal kanalını yalnız albedo ile birlikte kabul eder.
        if (hasNormal) filter.setImage("normal", const_cast<float*>(normal->data()), oidn::Format::Float3, w, h);
        filter.set("hdr", true);
        switch (quality) {
            case Quality::Fast: filter.set("quality", oidn::Quality::Fast); break;
            case Quality::Balanced: filter.set("quality", oidn::Quality::Balanced); break;
            case Quality::High: filter.set("quality", oidn::Quality::High); break;
        }
        filter.commit();
        filter.execute();

        const char* errorMessage = nullptr;
        if (m_impl->device.getError(errorMessage) != oidn::Error::None) {
            std::cerr << "OIDN error: " << (errorMessage ? errorMessage : "unknown") << std::endl;
            softBlurDenoise(color);
            return false;
        }
        std::copy(outBuf.begin(), outBuf.end(), color.data());
        return true;
    } catch (const std::exception& e) {
        std::cerr << "OIDN exception: " << e.what() << std::endl;
        softBlurDenoise(color);
        return false;
    }
#endif
}

namespace {
Denoiser& sharedDenoiser() {
    static Denoiser d;
    return d;
}
} // namespace

bool denoiseImage(Image& color, const Image* albedo, const Image* normal) {
    if (!denoiseAvailable()) return softBlurDenoise(color);
    return sharedDenoiser().run(color, albedo, normal, Denoiser::Quality::High);
}

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

} // namespace photon
