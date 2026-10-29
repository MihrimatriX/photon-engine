// zip.h — Küçük ZIP okuyucu/yazıcı (.photonpack paketleri için).
//
// Biçim: PKWARE APPNOTE'un temel alt kümesi. Yöntem 0 (saklama) ve 8 (DEFLATE), UTF-8 adlar
// (bayrak bit 11), Zip64 yok: arşiv < 4 GB, < 65535 girdi. Sıkıştırma ve açma stb'nin zlib
// kodlarıyla yapılır (stb_image_write / stb_image; projede zaten derleniyor).
//
// Okuyucu güvenilmeyen dosyaya karşı yazılmıştır (paketler başkasından gelebilir):
//   - girdi adları denetlenir: mutlak yol, sürücü harfi, ters bölü, ".." bileşeni, denetim
//     karakteri reddedilir ("zip slip": paket dışındaki bir dosyanın üzerine yazılması);
//   - CRC-32 ve açılmış boyut doğrulanır; şifreli girdi ve bilinmeyen yöntem reddedilir;
//   - boyut sınırları (girdi başına ve toplam) bellek tüketme saldırısını sınırlar.
#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace photon {

/// Paket içi ad güvenli mi? ("a/b.png" evet; "/a", "C:x", "a\\b", "../a", "a/../b" hayır.)
bool isSafeZipEntryName(const std::string& name);

uint32_t crc32(const void* data, size_t size, uint32_t crc = 0);

class ZipWriter {
public:
    ZipWriter() = default;
    ~ZipWriter();
    ZipWriter(const ZipWriter&) = delete;
    ZipWriter& operator=(const ZipWriter&) = delete;

    bool open(const std::filesystem::path& path, std::string* error = nullptr);
    /// @p compress: DEFLATE dene (küçülmezse saklanır). Ad isSafeZipEntryName olmalıdır.
    bool add(const std::string& name, const void* data, size_t size, bool compress, std::string* error = nullptr);
    bool addFile(const std::string& name, const std::filesystem::path& file, bool compress, std::string* error = nullptr);
    /// Merkezi dizini yazar ve dosyayı kapatır.
    bool close(std::string* error = nullptr);

private:
    struct Entry {
        std::string name;
        uint16_t method = 0;
        uint32_t crc = 0;
        uint32_t compressed = 0;
        uint32_t uncompressed = 0;
        uint32_t offset = 0;
    };
    std::FILE* m_file = nullptr;
    std::vector<Entry> m_entries;
    uint64_t m_offset = 0;
};

class ZipReader {
public:
    struct Entry {
        std::string name;
        uint16_t method = 0;
        uint32_t crc = 0;
        uint64_t compressed = 0;
        uint64_t uncompressed = 0;
        uint64_t localOffset = 0;
    };

    ZipReader() = default;
    ~ZipReader();
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;

    /// Merkezi dizini okur ve her girdiyi denetler (güvensiz ad varsa açma başarısız olur).
    bool open(const std::filesystem::path& path, std::string* error = nullptr);
    const std::vector<Entry>& entries() const { return m_entries; }
    const Entry* find(const std::string& name) const;
    /// Girdiyi belleğe açar; CRC ve boyut doğrulanır.
    bool read(const Entry& e, std::vector<uint8_t>& out, std::string* error = nullptr) const;
    /// Tüm girdileri @p dir altına çıkarır (klasörler oluşturulur).
    bool extractAll(const std::filesystem::path& dir, std::string* error = nullptr) const;

    static constexpr uint64_t kMaxEntryBytes = 4ull << 30;  ///< girdi başına (açılmış)
    static constexpr uint64_t kMaxTotalBytes = 16ull << 30; ///< tüm arşiv (açılmış)

private:
    std::FILE* m_file = nullptr;
    std::vector<Entry> m_entries;
};

} // namespace photon
