// cryptomatte.h — Psyop Cryptomatte 1.2.0: MurmurHash3-32 → IEEE-754 float32 kimlik karması,
// piksel başına kapsama sıralı (ID, coverage) birikimi ve OpenEXR manifest üstverisi.
// Nuke, Blender, Fusion ve After Effects ile birebir uyumlu nesne/malzeme maskeleri üretir.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace photon {

/// Psyop Cryptomatte 1.2.0 MurmurHash3_x86_32 (seed = 0).
uint32_t cryptoMurmurHash3_32(const void* key, size_t len, uint32_t seed = 0);
uint32_t cryptoHashName(const std::string& name);

/// Cryptomatte 1.2.0 uint32 → IEEE-754 float32 dönüşümü:
/// NaN/Inf (exponent = 255) ve denormal/sıfır (exponent = 0) üsleri tek bit çevrilerek
/// güvenli sonlu 32-bit float'lara eşlenir.
float cryptoHashToFloat(uint32_t hash);
float cryptoNameToFloat(const std::string& name);

/// 8 karakter küçük harf onaltılık (manifest değeri: "0a1b2c3d").
std::string cryptoHashHex(uint32_t hash);

/// Katman adından 7 haneli üstveri öneki: "cryptomatte/<7hex>/"
std::string cryptoMetadataPrefix(const std::string& layerName);

/// 4 kanallı (RGBA) 32-bit tam duyarlıklı Cryptomatte rank katmanı:
/// R = id_{2k}, G = cov_{2k}, B = id_{2k+1}, A = cov_{2k+1}.
struct CryptoRankImage {
    int width = 0;
    int height = 0;
    std::vector<float> rgba; ///< w * h * 4 (satır 0 = üst)
};

/// Piksel başına Cryptomatte örnek biriktiricisi (nesne ya da malzeme katmanı).
/// Farklı piksellere eşzamanlı addSample çağrıları güvenlidir (Film ile aynı kural).
class CryptoFilm {
public:
    CryptoFilm() = default;
    CryptoFilm(int width, int height);

    void resize(int width, int height);
    void clear();

    /// Manifest tablosuna (ad → MurmurHash3_32) giriş ekler; aynı ad tekrar eklenirse etkisiz.
    float registerName(const std::string& name);

    /// (x, y) pikseline bir örnek ekler. idHash = 0.0f → arka plan (boş kapsama).
    void addSample(int x, int y, float idHash);

    /// (x, y) pikseline ağırlıklı / çok örnekli katkı ekler (GPU geri okuma ve merge için).
    void addSamples(int x, int y, float idHash, uint32_t count);
    void addTotalSamples(int x, int y, uint32_t totalDelta);

    /// Başka bir CryptoFilm'i (ör. Hibrit GPU+CPU) bu filme birleştirir.
    void merge(const CryptoFilm& other);

    /// En baskın 2 * rankPairs kimliği kapsama sırasıyla çözer (varsayılan 3 katman = 6 rank).
    std::vector<CryptoRankImage> resolveRanks(int rankPairs = 3) const;

    /// Tek bir pikselde (id, coverage) sıralı listesini döndürür (testler için).
    std::vector<std::pair<float, float>> pixelRanks(int x, int y) const;

    /// Psyop Cryptomatte 1.2.0 JSON manifest dizgesi: {"Ad":"0123abcd",...}
    std::string manifestJson() const;

    /// OpenEXR üstveri (anahtar, değer) çiftleri: name, hash, conversion, manifest.
    std::vector<std::pair<std::string, std::string>> exrMetadata(const std::string& layerName) const;

    int width() const { return m_width; }
    int height() const { return m_height; }
    const std::unordered_map<std::string, uint32_t>& manifest() const { return m_manifest; }

private:
    struct PixelEntry {
        float id = 0.0f;
        uint32_t count = 0;
    };
    struct PixelBucket {
        uint32_t total = 0;
        std::vector<PixelEntry> entries;
    };

    int m_width = 0;
    int m_height = 0;
    std::vector<PixelBucket> m_pixels;
    std::unordered_map<std::string, uint32_t> m_manifest;
};

} // namespace photon
