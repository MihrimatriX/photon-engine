// zip.cpp — ZIP yerel/merkezi başlıkları, CRC-32, stb zlib ile DEFLATE.
#include "core/io/zip.h"
#include "core/platform/path.h"

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <memory>

// stb_image_write'ın zlib sıkıştırıcısı (image_io.cpp'de derlenir; başlıkta bildirilmez).
extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len, int quality);

namespace photon {

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kLocalSig = 0x04034b50u;
constexpr uint32_t kCentralSig = 0x02014b50u;
constexpr uint32_t kEndSig = 0x06054b50u;
constexpr uint16_t kFlagUtf8 = 0x0800u;
constexpr uint16_t kVersion = 20; // 2.0: DEFLATE

const std::array<uint32_t, 256>& crcTable() {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    return table;
}

void put16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

bool fail(std::string* error, const std::string& msg) {
    if (error) *error = msg;
    return false;
}

std::FILE* openFile(const fs::path& p, const char* mode) {
#ifdef _WIN32
    std::wstring m(mode, mode + std::strlen(mode));
    return _wfopen(p.c_str(), m.c_str());
#else
    return std::fopen(p.c_str(), mode);
#endif
}

bool seekTo(std::FILE* f, uint64_t pos) {
#ifdef _WIN32
    return _fseeki64(f, static_cast<long long>(pos), SEEK_SET) == 0;
#else
    return fseeko(f, static_cast<off_t>(pos), SEEK_SET) == 0;
#endif
}

uint64_t fileSize(std::FILE* f) {
#ifdef _WIN32
    if (_fseeki64(f, 0, SEEK_END) != 0) return 0;
    return static_cast<uint64_t>(_ftelli64(f));
#else
    if (fseeko(f, 0, SEEK_END) != 0) return 0;
    return static_cast<uint64_t>(ftello(f));
#endif
}

} // namespace

uint32_t crc32(const void* data, size_t size, uint32_t crc) {
    const auto& t = crcTable();
    const auto* p = static_cast<const uint8_t*>(data);
    crc = ~crc;
    for (size_t i = 0; i < size; ++i) crc = t[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

bool isSafeZipEntryName(const std::string& name) {
    if (name.empty() || name.size() > 1024) return false;
    if (name.front() == '/' || name.back() == '/') return false;
    for (const char ch : name) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == 0x7F || c == '\\' || c == ':') return false;
    }
    size_t start = 0;
    while (start <= name.size()) {
        const size_t end = name.find('/', start);
        const std::string part = name.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

// ── Yazıcı ───────────────────────────────────────────────────────────────

ZipWriter::~ZipWriter() {
    if (m_file) std::fclose(m_file);
}

bool ZipWriter::open(const fs::path& path, std::string* error) {
    m_file = openFile(path, "wb");
    m_entries.clear();
    m_offset = 0;
    return m_file ? true : fail(error, "Paket dosyası yazılamıyor: " + pathToUtf8(path));
}

bool ZipWriter::add(const std::string& name, const void* data, size_t size, bool compress, std::string* error) {
    if (!m_file) return fail(error, "Paket açık değil");
    if (!isSafeZipEntryName(name)) return fail(error, "Geçersiz paket içi ad: " + name);
    if (size > 0xFFFFFFFFull - 1 || m_entries.size() >= 0xFFFF) return fail(error, "Paket çok büyük (Zip64 yok): " + name);
    Entry e;
    e.name = name;
    e.crc = crc32(data, size);
    e.uncompressed = static_cast<uint32_t>(size);
    const uint8_t* payload = static_cast<const uint8_t*>(data);
    size_t payloadSize = size;
    std::unique_ptr<unsigned char, decltype(&std::free)> deflated(nullptr, &std::free);
    if (compress && size > 64 && size < static_cast<size_t>(INT_MAX)) {
        int outLen = 0;
        deflated.reset(stbi_zlib_compress(const_cast<unsigned char*>(static_cast<const unsigned char*>(data)),
                                          static_cast<int>(size), &outLen, 8));
        // zlib akışı: 2 bayt başlık + ham DEFLATE + 4 bayt Adler-32. ZIP ham DEFLATE ister.
        if (deflated && outLen > 6 && static_cast<size_t>(outLen - 6) < size) {
            payload = deflated.get() + 2;
            payloadSize = static_cast<size_t>(outLen - 6);
            e.method = 8;
        }
    }
    e.compressed = static_cast<uint32_t>(payloadSize);
    if (m_offset > 0xFFFFFFFFull - 30 - name.size() - payloadSize) return fail(error, "Paket 4 GB sınırını aşıyor");
    e.offset = static_cast<uint32_t>(m_offset);

    std::vector<uint8_t> h;
    put32(h, kLocalSig);
    put16(h, kVersion);
    put16(h, kFlagUtf8);
    put16(h, e.method);
    put16(h, 0);      // saat
    put16(h, 0x21);   // tarih: 1980-01-01 (yeniden üretilebilir paketler)
    put32(h, e.crc);
    put32(h, e.compressed);
    put32(h, e.uncompressed);
    put16(h, static_cast<uint16_t>(name.size()));
    put16(h, 0);
    h.insert(h.end(), name.begin(), name.end());
    if (std::fwrite(h.data(), 1, h.size(), m_file) != h.size() ||
        (payloadSize > 0 && std::fwrite(payload, 1, payloadSize, m_file) != payloadSize))
        return fail(error, "Paket yazma hatası (disk dolu mu?)");
    m_offset += h.size() + payloadSize;
    m_entries.push_back(std::move(e));
    return true;
}

bool ZipWriter::addFile(const std::string& name, const fs::path& file, bool compress, std::string* error) {
    std::FILE* f = openFile(file, "rb");
    if (!f) return fail(error, "Dosya okunamadı: " + pathToUtf8(file));
    const uint64_t size = fileSize(f);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    const bool ok = seekTo(f, 0) && (size == 0 || std::fread(buf.data(), 1, buf.size(), f) == buf.size());
    std::fclose(f);
    if (!ok) return fail(error, "Dosya okunamadı: " + pathToUtf8(file));
    return add(name, buf.data(), buf.size(), compress, error);
}

bool ZipWriter::close(std::string* error) {
    if (!m_file) return fail(error, "Paket açık değil");
    std::vector<uint8_t> cd;
    for (const Entry& e : m_entries) {
        put32(cd, kCentralSig);
        put16(cd, kVersion);
        put16(cd, kVersion);
        put16(cd, kFlagUtf8);
        put16(cd, e.method);
        put16(cd, 0);
        put16(cd, 0x21);
        put32(cd, e.crc);
        put32(cd, e.compressed);
        put32(cd, e.uncompressed);
        put16(cd, static_cast<uint16_t>(e.name.size()));
        put16(cd, 0); // ek alan
        put16(cd, 0); // yorum
        put16(cd, 0); // disk
        put16(cd, 0); // iç öznitelik
        put32(cd, 0); // dış öznitelik
        put32(cd, e.offset);
        cd.insert(cd.end(), e.name.begin(), e.name.end());
    }
    std::vector<uint8_t> end;
    put32(end, kEndSig);
    put16(end, 0);
    put16(end, 0);
    put16(end, static_cast<uint16_t>(m_entries.size()));
    put16(end, static_cast<uint16_t>(m_entries.size()));
    put32(end, static_cast<uint32_t>(cd.size()));
    put32(end, static_cast<uint32_t>(m_offset));
    put16(end, 0);
    const bool ok = std::fwrite(cd.data(), 1, cd.size(), m_file) == cd.size() &&
                    std::fwrite(end.data(), 1, end.size(), m_file) == end.size();
    const bool closed = std::fclose(m_file) == 0;
    m_file = nullptr;
    return (ok && closed) ? true : fail(error, "Paket yazma hatası (disk dolu mu?)");
}

// ── Okuyucu ──────────────────────────────────────────────────────────────

ZipReader::~ZipReader() {
    if (m_file) std::fclose(m_file);
}

bool ZipReader::open(const fs::path& path, std::string* error) {
    m_entries.clear();
    m_file = openFile(path, "rb");
    if (!m_file) return fail(error, "Paket açılamadı: " + pathToUtf8(path));
    const uint64_t size = fileSize(m_file);
    if (size < 22) return fail(error, "Geçersiz paket (çok kısa)");
    // Dizin sonu kaydı: sondan en çok 22 + 65535 (yorum) bayt içinde.
    const uint64_t tail = std::min<uint64_t>(size, 22 + 65535);
    std::vector<uint8_t> buf(static_cast<size_t>(tail));
    if (!seekTo(m_file, size - tail) || std::fread(buf.data(), 1, buf.size(), m_file) != buf.size())
        return fail(error, "Paket okunamadı");
    size_t endPos = std::string::npos;
    for (size_t i = buf.size() - 22 + 1; i-- > 0;)
        if (get32(&buf[i]) == kEndSig) {
            endPos = i;
            break;
        }
    if (endPos == std::string::npos) return fail(error, "Geçersiz paket (ZIP dizini yok)");
    const uint8_t* end = &buf[endPos];
    const uint16_t count = get16(end + 10);
    const uint32_t cdSize = get32(end + 12);
    const uint32_t cdOffset = get32(end + 16);
    if (count == 0xFFFF || cdOffset == 0xFFFFFFFFu) return fail(error, "Zip64 paketleri desteklenmiyor");
    if (static_cast<uint64_t>(cdOffset) + cdSize > size) return fail(error, "Bozuk paket (dizin sınır dışında)");
    std::vector<uint8_t> cd(cdSize);
    if (!seekTo(m_file, cdOffset) || (cdSize > 0 && std::fread(cd.data(), 1, cd.size(), m_file) != cd.size()))
        return fail(error, "Paket dizini okunamadı");
    size_t p = 0;
    uint64_t total = 0;
    for (uint16_t i = 0; i < count; ++i) {
        if (p + 46 > cd.size() || get32(&cd[p]) != kCentralSig) return fail(error, "Bozuk paket dizini");
        const uint16_t flags = get16(&cd[p + 8]);
        Entry e;
        e.method = get16(&cd[p + 10]);
        e.crc = get32(&cd[p + 16]);
        e.compressed = get32(&cd[p + 20]);
        e.uncompressed = get32(&cd[p + 24]);
        const uint16_t nameLen = get16(&cd[p + 28]);
        const uint16_t extraLen = get16(&cd[p + 30]);
        const uint16_t commentLen = get16(&cd[p + 32]);
        e.localOffset = get32(&cd[p + 42]);
        if (p + 46 + nameLen > cd.size()) return fail(error, "Bozuk paket dizini");
        e.name.assign(reinterpret_cast<const char*>(&cd[p + 46]), nameLen);
        p += 46 + static_cast<size_t>(nameLen) + extraLen + commentLen;
        if (!e.name.empty() && e.name.back() == '/') continue; // klasör girdisi
        if (flags & 0x0001u) return fail(error, "Şifreli paket girdisi desteklenmiyor: " + e.name);
        if (e.method != 0 && e.method != 8) return fail(error, "Desteklenmeyen sıkıştırma: " + e.name);
        if (!isSafeZipEntryName(e.name)) return fail(error, "Güvensiz paket içi yol reddedildi: " + e.name);
        if (e.uncompressed > kMaxEntryBytes) return fail(error, "Paket girdisi çok büyük: " + e.name);
        total += e.uncompressed;
        if (total > kMaxTotalBytes) return fail(error, "Paket açılınca çok büyük");
        if (e.localOffset >= size) return fail(error, "Bozuk paket girdisi: " + e.name);
        m_entries.push_back(std::move(e));
    }
    return true;
}

const ZipReader::Entry* ZipReader::find(const std::string& name) const {
    for (const Entry& e : m_entries)
        if (e.name == name) return &e;
    return nullptr;
}

bool ZipReader::read(const Entry& e, std::vector<uint8_t>& out, std::string* error) const {
    if (!m_file) return fail(error, "Paket açık değil");
    uint8_t h[30];
    if (!seekTo(m_file, e.localOffset) || std::fread(h, 1, 30, m_file) != 30 || get32(h) != kLocalSig)
        return fail(error, "Bozuk paket girdisi: " + e.name);
    const uint64_t dataPos = e.localOffset + 30 + get16(h + 26) + get16(h + 28);
    if (e.compressed > kMaxEntryBytes || !seekTo(m_file, dataPos)) return fail(error, "Bozuk paket girdisi: " + e.name);
    std::vector<uint8_t> raw(static_cast<size_t>(e.compressed));
    if (!raw.empty() && std::fread(raw.data(), 1, raw.size(), m_file) != raw.size())
        return fail(error, "Paket girdisi eksik: " + e.name);
    if (e.method == 0) {
        out = std::move(raw);
    } else {
        if (raw.size() > static_cast<size_t>(INT_MAX)) return fail(error, "Paket girdisi çok büyük: " + e.name);
        int outLen = 0;
        std::unique_ptr<char, decltype(&std::free)> inflated(
            stbi_zlib_decode_noheader_malloc(reinterpret_cast<const char*>(raw.data()), static_cast<int>(raw.size()), &outLen),
            &std::free);
        if (!inflated || outLen < 0) return fail(error, "Paket girdisi açılamadı (bozuk DEFLATE): " + e.name);
        out.assign(reinterpret_cast<const uint8_t*>(inflated.get()), reinterpret_cast<const uint8_t*>(inflated.get()) + outLen);
    }
    if (out.size() != e.uncompressed || crc32(out.data(), out.size()) != e.crc)
        return fail(error, "Paket girdisi bozuk (CRC uyuşmuyor): " + e.name);
    return true;
}

bool ZipReader::extractAll(const fs::path& dir, std::string* error) const {
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path root = fs::weakly_canonical(dir, ec);
    std::vector<uint8_t> data;
    for (const Entry& e : m_entries) {
        const fs::path target = (dir / pathFromUtf8(e.name)).lexically_normal();
        // Ad denetimine ek savunma: hedef çıkarma klasörünün içinde kalmalı.
        const fs::path canon = fs::weakly_canonical(target, ec);
        const auto rel = canon.lexically_relative(root);
        if (rel.empty() || *rel.begin() == "..") return fail(error, "Güvensiz paket içi yol reddedildi: " + e.name);
        if (!read(e, data, error)) return false;
        fs::create_directories(target.parent_path(), ec);
        std::FILE* f = openFile(target, "wb");
        if (!f) return fail(error, "Çıkarılamadı: " + pathToUtf8(target));
        const bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
        if (std::fclose(f) != 0 || !ok) return fail(error, "Çıkarma hatası (disk dolu mu?): " + pathToUtf8(target));
    }
    return true;
}

} // namespace photon
