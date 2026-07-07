#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_STB_ZLIB 1
#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"

#include "core/image/image_io.h"
#include <iostream>
#include <vector>

namespace photon {

bool saveImagePNG(const Image& img, const std::string& path, ToneMapOperator tmo, float exposure) {
    int w = img.width();
    int h = img.height();
    
    std::vector<uint8_t> ldrData(w * h * 3);
    
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Color3f hdrColor = img.getPixel(x, y);
            Color3f ldrColor = toneMap(hdrColor, tmo, exposure);
            
            int idx = (y * w + x) * 3;
            ldrData[idx + 0] = static_cast<uint8_t>(std::clamp(ldrColor.r * 255.0f, 0.0f, 255.0f));
            ldrData[idx + 1] = static_cast<uint8_t>(std::clamp(ldrColor.g * 255.0f, 0.0f, 255.0f));
            ldrData[idx + 2] = static_cast<uint8_t>(std::clamp(ldrColor.b * 255.0f, 0.0f, 255.0f));
        }
    }
    
    int stride = w * 3;
    int success = stbi_write_png(path.c_str(), w, h, 3, ldrData.data(), stride);
    return success != 0;
}

bool saveImageEXR(const Image& img, const std::string& path) {
    int w = img.width();
    int h = img.height();
    
    // TinyEXR requires separate channels
    std::vector<float> r(w * h);
    std::vector<float> g(w * h);
    std::vector<float> b(w * h);
    
    for (int i = 0; i < w * h; ++i) {
        // Retrieve pixel from Image data buffer directly for performance
        // Image data layout: RGBRGB...
        r[i] = img.data()[i * 3 + 0];
        g[i] = img.data()[i * 3 + 1];
        b[i] = img.data()[i * 3 + 2];
    }
    
    EXRHeader header;
    InitEXRHeader(&header);
    
    EXRImage exrImage;
    InitEXRImage(&exrImage);
    
    exrImage.num_channels = 3;
    
    std::vector<float*> images(3);
    images[0] = b.data(); // EXR expects BGR order often but channels specify names
    images[1] = g.data();
    images[2] = r.data();
    exrImage.images = reinterpret_cast<unsigned char**>(images.data());
    exrImage.width = w;
    exrImage.height = h;
    
    header.num_channels = 3;
    EXRChannelInfo channelInfos[3];
    header.channels = channelInfos;
    
    // Channel names must be B, G, R
    strncpy(header.channels[0].name, "B", 255);
    header.channels[0].name[255] = '\0';
    strncpy(header.channels[1].name, "G", 255);
    header.channels[1].name[255] = '\0';
    strncpy(header.channels[2].name, "R", 255);
    header.channels[2].name[255] = '\0';
    
    int pixelTypes[3];
    int requestedPixelTypes[3];
    pixelTypes[0] = TINYEXR_PIXELTYPE_FLOAT;
    pixelTypes[1] = TINYEXR_PIXELTYPE_FLOAT;
    pixelTypes[2] = TINYEXR_PIXELTYPE_FLOAT;
    requestedPixelTypes[0] = TINYEXR_PIXELTYPE_FLOAT;
    requestedPixelTypes[1] = TINYEXR_PIXELTYPE_FLOAT;
    requestedPixelTypes[2] = TINYEXR_PIXELTYPE_FLOAT;
    
    header.pixel_types = pixelTypes;
    header.requested_pixel_types = requestedPixelTypes;
    
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
    int w, h, channels;
    float* data = stbi_loadf(path.c_str(), &w, &h, &channels, 3); // force 3 channels (RGB)
    
    if (!data) {
        std::cerr << "HDR Load Error: " << stbi_failure_reason() << " for path: " << path << std::endl;
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

std::optional<Image> loadImageLDR(const std::string& path) {
    int w, h, channels;
    uint8_t* data = stbi_load(path.c_str(), &w, &h, &channels, 3); // force RGB
    
    if (!data) {
        std::cerr << "LDR Load Error: " << stbi_failure_reason() << " for path: " << path << std::endl;
        return std::nullopt;
    }
    
    Image img(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = (y * w + x) * 3;
            Color3f srgb(
                data[idx + 0] / 255.0f,
                data[idx + 1] / 255.0f,
                data[idx + 2] / 255.0f
            );
            img.setPixel(x, y, srgb.sRGBToLinear());
        }
    }
    
    stbi_image_free(data);
    return img;
}

} // namespace photon
