// image_io.cpp — Görüntü dosyası G/Ç uygulaması: stb_image (PNG/JPG/HDR okuma),
// stb_image_write (PNG/JPG yazma) ve tinyexr (EXR okuma/yazma). Bu dosya her üç tek-başlık
// kütüphanenin uygulamasını (IMPLEMENTATION makroları) derleyen tek yerdir.
// Akış (yazma): doğrusal radyans → pozlama (EV) → ton eşleme → sRGB → dither → 8-bit.
// Narrow paths are UTF-8 on every platform; on Windows stb converts them to UTF-16.
#define STBI_WINDOWS_UTF8
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STBIW_WINDOWS_UTF8
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_STB_ZLIB 1
#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"

#include "core/image/image_io.h"
#include "core/color/transfer.h"
#include "core/platform/path.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>

namespace photon {
namespace {

// ponytail: ceiling — 512MB file, 16384 on a side. A larger texture needs tiling.
constexpr uintmax_t kMaxImageFileBytes = 512ull * 1024ull * 1024ull;
constexpr int kMaxImageDim = 16384;

bool imageFileOk(const std::string& path) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(pathFromUtf8(path), ec);
    return !ec && sz <= kMaxImageFileBytes;
}

bool imageDimsOk(int w, int h) {
    return w > 0 && h > 0 && w <= kMaxImageDim && h <= kMaxImageDim;
}

} // namespace


// Her piksel: toneMap() pozlama + ton eşleme + sRGB kodlamayı yapar (sonuç [0,1] ekran
// değeri); ardından kanal başına deterministik TPDF dither ile 8-bit'e yuvarlanır.
// Dither piksel konumunun saf fonksiyonu olduğu için aynı render aynı dosyayı üretir.
bool saveImagePNG(const Image& img, const std::string& path, ToneMapOperator tmo, float exposureEV,
                  bool dither) {
    const int w = img.width();
    const int h = img.height();
    if (w <= 0 || h <= 0) return false;

    std::vector<uint8_t> ldrData(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Color3f display = toneMap(img.getPixel(x, y), tmo, exposureEV);
            const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 3;
            for (int c = 0; c < 3; ++c) {
                const float d = dither ? tpdfDither(x, y, c) : 0.0f;
                ldrData[idx + static_cast<size_t>(c)] = quantizeUnorm8(display[c], d);
            }
        }
    }

    // Image rows are top first, which is also PNG's order.
    return stbi_write_png(path.c_str(), w, h, 3, ldrData.data(), w * 3) != 0;
}

// Şeffaf arka planlı PNG. Film'deki renk "önceden çarpılmış" (premultiplied)
// birikir: kenardaki bir pikselin örneklerinin yarısı nesneye çarptıysa renk
// toplamı da yarı yarıya siyah arka planla karışır. PNG düz (straight) alfa
// beklediği için renk alfa'ya bölünür, sonra ton eşlenir.
bool saveImagePNGAlpha(const Image& img, const Image& alpha, const std::string& path,
                       ToneMapOperator tmo, float exposureEV) {
    const int w = img.width();
    const int h = img.height();
    if (w <= 0 || h <= 0 || alpha.width() != w || alpha.height() != h) return false;
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float a = std::clamp(alpha.getPixel(x, y).r, 0.0f, 1.0f);
            const Color3f straight = a > 1e-4f ? img.getPixel(x, y) / a : Color3f::black();
            const Color3f display = toneMap(straight, tmo, exposureEV);
            const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4;
            for (int c = 0; c < 3; ++c)
                rgba[idx + static_cast<size_t>(c)] = quantizeUnorm8(display[c], tpdfDither(x, y, c));
            rgba[idx + 3] = quantizeUnorm8(a);
        }
    }
    return stbi_write_png(path.c_str(), w, h, 4, rgba.data(), w * 4) != 0;
}

bool saveImageJPG(const Image& img, const std::string& path, ToneMapOperator tmo, float exposureEV,
                  int quality) {
    const int w = img.width();
    const int h = img.height();
    if (w <= 0 || h <= 0) return false;
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Color3f display = toneMap(img.getPixel(x, y), tmo, exposureEV);
            const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 3;
            for (int c = 0; c < 3; ++c)
                rgb[idx + static_cast<size_t>(c)] = quantizeUnorm8(display[c], tpdfDither(x, y, c));
        }
    }
    return stbi_write_jpg(path.c_str(), w, h, 3, rgb.data(), std::clamp(quality, 1, 100)) != 0;
}

bool saveRGBA8PNG(const uint8_t* rgba, int w, int h, const std::string& path, bool flipVertically) {
    if (!rgba || w <= 0 || h <= 0) return false;
    if (!flipVertically) return stbi_write_png(path.c_str(), w, h, 4, rgba, w * 4) != 0;
    std::vector<uint8_t> flipped(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    const size_t row = static_cast<size_t>(w) * 4;
    for (int y = 0; y < h; ++y)
        std::memcpy(&flipped[static_cast<size_t>(y) * row], rgba + static_cast<size_t>(h - 1 - y) * row, row);
    return stbi_write_png(path.c_str(), w, h, 4, flipped.data(), static_cast<int>(row)) != 0;
}

bool saveImageEXR(const Image& img, const std::string& path) {
    const int w = img.width();
    const int h = img.height();
    if (w <= 0 || h <= 0) return false;
    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);

    // EXR stores planar channels; readers expect them sorted by name: B, G, R.
    // EXR ton eşleme/sRGB uygulanmamış sahne-doğrusal (scene-linear) radyansı saklar;
    // 1'in üstündeki değerler kırpılmaz, sonradan kompozisyon/renk düzeltmede kullanılabilir.
    // Image iç içe RGB tutar → burada kanal başına ayrı düzlemlere (planar) ayrılır.
    std::vector<float> r(n), g(n), b(n);
    const float* px = img.data();
    for (size_t i = 0; i < n; ++i) {
        r[i] = px[i * 3 + 0];
        g[i] = px[i * 3 + 1];
        b[i] = px[i * 3 + 2];
    }

    EXRHeader header;
    InitEXRHeader(&header);
    EXRImage exrImage;
    InitEXRImage(&exrImage);

    float* planes[3] = {b.data(), g.data(), r.data()};
    exrImage.num_channels = 3;
    exrImage.images = reinterpret_cast<unsigned char**>(planes);
    exrImage.width = w;
    exrImage.height = h;

    EXRChannelInfo channels[3];
    std::memset(channels, 0, sizeof(channels));
    channels[0].name[0] = 'B';
    channels[1].name[0] = 'G';
    channels[2].name[0] = 'R';
    header.num_channels = 3;
    header.channels = channels;

    // Input is float; stored as half (ample for radiance, half the size).
    // half = 16-bit float: ~3 ondalık basamak hassasiyet, ~6·10⁻⁸ … 65504 aralığı; HDR
    // radyans için yeterli. ZIP kayıpsız sıkıştırmadır.
    int pixelTypes[3] = {TINYEXR_PIXELTYPE_FLOAT, TINYEXR_PIXELTYPE_FLOAT, TINYEXR_PIXELTYPE_FLOAT};
    int storedTypes[3] = {TINYEXR_PIXELTYPE_HALF, TINYEXR_PIXELTYPE_HALF, TINYEXR_PIXELTYPE_HALF};
    header.pixel_types = pixelTypes;
    header.requested_pixel_types = storedTypes;
    header.compression_type = TINYEXR_COMPRESSIONTYPE_ZIP;

    const char* err = nullptr;
    int ret = SaveEXRImageToFile(&exrImage, &header, path.c_str(), &err);
    if (ret != TINYEXR_SUCCESS) {
        std::cerr << "EXR Save Error: " << (err ? err : "unknown") << std::endl;
        FreeEXRErrorMessage(err);
        return false;
    }
    return true;
}

std::optional<Image> loadImageHDR(const std::string& path) {
    if (!imageFileOk(path)) {
        std::cerr << "HDR Load Error: missing or oversized file: " << path << std::endl;
        return std::nullopt;
    }
    int w = 0, h = 0, channels = 0;
    if (!stbi_info(path.c_str(), &w, &h, &channels)) {
        std::cerr << "HDR Load Error: " << stbi_failure_reason() << " for path: " << path << std::endl;
        return std::nullopt;
    }
    // stbi_loadf would also accept a PNG and decode it with a pow-2.2 guess.
    if (!stbi_is_hdr(path.c_str())) {
        std::cerr << "HDR Load Error: not a Radiance HDR file: " << path << std::endl;
        return std::nullopt;
    }
    if (!imageDimsOk(w, h)) {
        std::cerr << "HDR Load Error: dimensions exceed limit for path: " << path << std::endl;
        return std::nullopt;
    }
    float* data = stbi_loadf(path.c_str(), &w, &h, &channels, 3); // force 3 channels (RGB)
    
    if (!data) {
        std::cerr << "HDR Load Error: " << stbi_failure_reason() << " for path: " << path << std::endl;
        return std::nullopt;
    }
    if (!imageDimsOk(w, h)) {
        stbi_image_free(data);
        return std::nullopt;
    }
    
    Image img(w, h);
    // Copy data
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = (y * w + x) * 3;
            img.setPixel(x, y, Color3f(data[idx + 0], data[idx + 1], data[idx + 2]));
        }
    }
    
    stbi_image_free(data);
    return img;
}

std::optional<Image> loadImageEXR(const std::string& path) {
    if (!imageFileOk(path)) {
        std::cerr << "EXR Load Error: missing or oversized file: " << path << std::endl;
        return std::nullopt;
    }
    EXRVersion version{};
    if (ParseEXRVersionFromFile(&version, path.c_str()) != TINYEXR_SUCCESS || version.multipart || version.non_image) {
        std::cerr << "EXR Load Error: unreadable or unsupported: " << path << std::endl;
        return std::nullopt;
    }
    EXRHeader header;
    InitEXRHeader(&header);
    const char* hdrErr = nullptr;
    if (ParseEXRHeaderFromFile(&header, &version, path.c_str(), &hdrErr) != TINYEXR_SUCCESS) {
        std::cerr << "EXR Load Error: " << (hdrErr ? hdrErr : "unknown") << std::endl;
        FreeEXRErrorMessage(hdrErr);
        FreeEXRHeader(&header);
        return std::nullopt;
    }
    int64_t w64 = static_cast<int64_t>(header.data_window.max_x) - header.data_window.min_x + 1;
    int64_t h64 = static_cast<int64_t>(header.data_window.max_y) - header.data_window.min_y + 1;
    FreeEXRHeader(&header);
    if (w64 <= 0 || h64 <= 0 || w64 > kMaxImageDim || h64 > kMaxImageDim) {
        std::cerr << "EXR Load Error: dimensions exceed limit: " << path << std::endl;
        return std::nullopt;
    }

    float* out_rgba = nullptr;
    int w = 0;
    int h = 0;
    const char* err = nullptr;
    
    int ret = LoadEXR(&out_rgba, &w, &h, path.c_str(), &err);
    if (ret != TINYEXR_SUCCESS) {
        std::cerr << "EXR Load Error: " << (err ? err : "unknown") << std::endl;
        FreeEXRErrorMessage(err);
        return std::nullopt;
    }
    if (!imageDimsOk(w, h)) {
        free(out_rgba);
        return std::nullopt;
    }
    
    Image img(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = (y * w + x) * 4; // LoadEXR returns RGBA
            img.setPixel(x, y, Color3f(out_rgba[idx + 0], out_rgba[idx + 1], out_rgba[idx + 2]));
        }
    }
    
    free(out_rgba);
    return img;
}

namespace {

// 8-bit değer yalnızca 256 farklı olabildiği için sRGB çözme (pow içerir) bir kez
// 256 elemanlı arama tablosuna (LUT) hesaplanır; piksel başına pow çağrısı yapılmaz.
std::optional<Image> imageFromRgb8(const uint8_t* data, int w, int h, TextureEncoding encoding) {
    if (!data || !imageDimsOk(w, h)) return std::nullopt;
    float lut[256];
    for (int i = 0; i < 256; ++i) {
        const float v = static_cast<float>(i) / 255.0f;
        lut[i] = encoding == TextureEncoding::SRGB ? srgbDecode(v) : v;
    }
    Image img(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 3;
            img.setPixel(x, y, Color3f(lut[data[idx + 0]], lut[data[idx + 1]], lut[data[idx + 2]]));
        }
    }
    return img;
}

} // namespace

std::optional<Image> loadImageLDR(const std::string& path, TextureEncoding encoding) {
    if (!imageFileOk(path)) {
        std::cerr << "LDR Load Error: missing or oversized file: " << path << std::endl;
        return std::nullopt;
    }
    int w = 0, h = 0, channels = 0;
    if (!stbi_info(path.c_str(), &w, &h, &channels)) {
        std::cerr << "LDR Load Error: " << stbi_failure_reason() << " for path: " << path << std::endl;
        return std::nullopt;
    }
    // stbi_load would clip an .hdr to 8 bits; HDR goes through loadImageHDR.
    if (stbi_is_hdr(path.c_str())) {
        std::cerr << "LDR Load Error: HDR file, use loadImageHDR: " << path << std::endl;
        return std::nullopt;
    }
    if (!imageDimsOk(w, h)) {
        std::cerr << "LDR Load Error: dimensions exceed limit for path: " << path << std::endl;
        return std::nullopt;
    }
    uint8_t* data = stbi_load(path.c_str(), &w, &h, &channels, 3); // force RGB

    if (!data) {
        std::cerr << "LDR Load Error: " << stbi_failure_reason() << " for path: " << path << std::endl;
        return std::nullopt;
    }
    auto img = imageFromRgb8(data, w, h, encoding);
    stbi_image_free(data);
    return img;
}

std::optional<Image> loadImageLDRMemory(const unsigned char* bytes, int size, TextureEncoding encoding) {
    if (!bytes || size <= 0) return std::nullopt;
    if (static_cast<uint64_t>(size) > kMaxImageFileBytes) return std::nullopt;
    int w = 0, h = 0, channels = 0;
    if (!stbi_info_from_memory(bytes, size, &w, &h, &channels) || !imageDimsOk(w, h)) return std::nullopt;
    if (stbi_is_hdr_from_memory(bytes, size)) return std::nullopt;
    uint8_t* data = stbi_load_from_memory(bytes, size, &w, &h, &channels, 3);
    if (!data) return std::nullopt;
    auto img = imageFromRgb8(data, w, h, encoding);
    stbi_image_free(data);
    return img;
}

} // namespace photon

namespace photon {

bool loadRGBA8(const std::string& path, std::vector<uint8_t>& out, int& w, int& h) {
    int channels = 0;
    if (!imageFileOk(path)) return false;
    uint8_t* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data) return false;
    if (!imageDimsOk(w, h)) {
        stbi_image_free(data);
        return false;
    }
    out.assign(data, data + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    stbi_image_free(data);
    return true;
}

} // namespace photon
