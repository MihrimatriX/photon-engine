// image.h — RGB görüntü: dokular, ortam (HDR) haritaları, çözülmüş render ve AOV'ler.
// İki depolama biçimi desteklenir:
//   - Float32: tam 96-bit float RGB (HDRI, EXR, çözülmüş film, AOV).
//   - U8Srgb / U8Linear: 24-bit 8-bit RGB + 256 girişli LUT (~4× RAM tasarrufu; LDR dokular).
// İsteğe bağlı mipmap piramidi (buildMipmaps / sampleMip) ışın konisi LOD için doğrusal
// ışıkta 2×2 kutu süzgeciyle kurulur.
#pragma once

#include "core/color/spectrum.h"

#include <vector>
#include <cstdint>
#include <cassert>
#include <algorithm>

namespace photon {

/// RGB image: Float32 (HDR/renders/AOVs) or compact 8-bit (LDR textures, 4x RAM savings).
/// Rows are stored top to bottom: (0, 0) is the top-left pixel.
/// Sample accumulation lives in Film (core/image/film.h).
class Image {
public:
    enum class StorageFormat : uint8_t { Float32, U8Srgb, U8Linear };

    Image() = default;
    Image(int width, int height);

    /// Kompakt 8-bit RGB görüntü oluşturur (U8Srgb veya U8Linear; ~4× daha az RAM).
    static Image createCompact8(int width, int height, const uint8_t* rgb8, StorageFormat fmt);

    void resize(int width, int height);
    void clear();

    void setPixel(int x, int y, const Color3f& color);
    Color3f getPixel(int x, int y) const;

    /// Bilinear sample with wrap UV in [0,1]. Uses raw pixel values (not accumulation averages).
    Color3f sampleBilinear(float u, float v) const;

    /// Doğrusal ışıkta 2×2 kutu süzgeciyle mipmap piramidi kurar (Ray Cone LOD için).
    void buildMipmaps();
    bool hasMipmaps() const { return !m_mipsF32.empty() || !m_mipsU8.empty(); }
    int mipLevels() const;

    /// Üçdoğrusal (trilinear) mipmap örneklemesi: lod = 0 en yüksek çözünürlük.
    Color3f sampleMip(float u, float v, float lod) const;

    int width() const { return m_width; }
    int height() const { return m_height; }
    StorageFormat storageFormat() const { return m_format; }
    bool isCompact8() const { return m_format != StorageFormat::Float32; }
    size_t memoryBytes() const;

    /// Float32 görüntüler için ham dizi erişimi (kompakt 8-bit'te boş dönebilir).
    const float* data() const { return m_data.data(); }
    float* data() { return m_data.data(); }
    const uint8_t* dataU8() const { return m_dataU8.data(); }
    size_t pixelCount() const { return static_cast<size_t>(m_width) * static_cast<size_t>(m_height); }

private:
    struct MipLevelF32 {
        int width = 0;
        int height = 0;
        std::vector<float> data;
    };
    struct MipLevelU8 {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> data;
    };

    Color3f sampleLevel(int level, float u, float v) const;
    Color3f decodeU8(uint8_t r, uint8_t g, uint8_t b) const;

    int m_width = 0;
    int m_height = 0;
    StorageFormat m_format = StorageFormat::Float32;
    std::vector<float> m_data;          // Float32: w * h * 3
    std::vector<uint8_t> m_dataU8;      // U8Srgb/U8Linear: w * h * 3
    std::vector<MipLevelF32> m_mipsF32; // level 1..N (level 0 = m_data)
    std::vector<MipLevelU8> m_mipsU8;   // level 1..N (level 0 = m_dataU8)

    size_t pixelIndex(int x, int y) const {
        assert(x >= 0 && x < m_width && y >= 0 && y < m_height);
        return (static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x)) * 3;
    }
};

} // namespace photon
