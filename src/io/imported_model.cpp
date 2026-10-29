// imported_model.cpp — Köşe birleştirme, sarma yönü düzeltme ve gömülü doku önbelleği.
#include "io/imported_model.h"
#include "core/platform/path.h"

#include <bit>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace photon {

namespace {

std::mutex gCacheMutex;
std::string gCacheDir;

uint64_t fnv1a64(const unsigned char* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

/// Bayt imzasından uzantı (stb'nin okuyabildiği biçimler).
const char* sniffImageExtension(const unsigned char* p, size_t n) {
    if (n >= 8 && std::memcmp(p, "\x89PNG\r\n\x1a\n", 8) == 0) return ".png";
    if (n >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) return ".jpg";
    if (n >= 2 && p[0] == 'B' && p[1] == 'M') return ".bmp";
    if (n >= 10 && std::memcmp(p, "#?RADIANCE", 10) == 0) return ".hdr";
    if (n >= 4 && p[0] == 0x76 && p[1] == 0x2f && p[2] == 0x31 && p[3] == 0x01) return ".exr";
    // TGA'nın sihirli sayısı yok: başlık alanlarından makul bir kontrol.
    if (n >= 18 && (p[2] == 2 || p[2] == 3 || p[2] == 10 || p[2] == 11) && (p[16] == 8 || p[16] == 24 || p[16] == 32))
        return ".tga";
    return nullptr;
}

} // namespace

bool MeshBuilder::Key::operator==(const Key& o) const { return std::memcmp(v, o.v, sizeof(v)) == 0; }

size_t MeshBuilder::KeyHash::operator()(const Key& k) const {
    uint64_t h = 1469598103934665603ull;
    for (uint32_t x : k.v) {
        h ^= x;
        h *= 1099511628211ull;
    }
    return static_cast<size_t>(h);
}

void MeshBuilder::addCorner(const Vec3f& p, const Vec3f& n, const Vec2f& uv) {
    // −0 ile +0 aynı köşe: bit karşılaştırmasından önce sıfırla.
    auto bits = [](float f) { return std::bit_cast<uint32_t>(f == 0.0f ? 0.0f : f); };
    const Key key{{bits(p.x), bits(p.y), bits(p.z), bits(n.x), bits(n.y), bits(n.z), bits(uv.x), bits(uv.y)}};
    if (n.lengthSquared() > 0.0f) m_hasNormals = true;
    auto [it, inserted] = m_lookup.try_emplace(key, static_cast<uint32_t>(m_p.size()));
    if (inserted) {
        m_p.push_back(p);
        m_n.push_back(n);
        m_uv.push_back(uv);
    }
    m_indices.push_back(it->second);
}

std::shared_ptr<TriangleMesh> MeshBuilder::build(const Material* material) {
    if (m_indices.empty()) return nullptr;
    if (!m_hasNormals) m_n.clear();
    else orientWindingToNormals(m_p, m_n, m_indices);
    auto mesh = std::make_shared<TriangleMesh>(m_p, m_n, m_uv, m_indices, material);
    m_lookup.clear();
    return mesh;
}

bool orientWindingToNormals(const std::vector<Vec3f>& positions, const std::vector<Vec3f>& normals,
                            std::vector<uint32_t>& indices) {
    if (normals.size() != positions.size() || indices.size() < 3) return false;
    // Alan ağırlıklı oy: büyük üçgenler küçük, belki bozuk olanlardan baskın.
    double vote = 0.0;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
        const Vec3f face = (positions[b] - positions[a]).cross(positions[c] - positions[a]);
        vote += face.dot(normals[a] + normals[b] + normals[c]);
    }
    if (!(vote < 0.0)) return false;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) std::swap(indices[i + 1], indices[i + 2]);
    return true;
}

void setEmbeddedTextureCacheDir(const std::string& dir) {
    std::lock_guard<std::mutex> lk(gCacheMutex);
    gCacheDir = dir;
}

std::string cacheEmbeddedTexture(const void* data, size_t size) {
    if (!data || size == 0) return {};
    const auto* bytes = static_cast<const unsigned char*>(data);
    const char* ext = sniffImageExtension(bytes, size);
    if (!ext) return {};
    namespace fs = std::filesystem;
    fs::path dir;
    {
        std::lock_guard<std::mutex> lk(gCacheMutex);
        dir = gCacheDir.empty() ? userDataDir() / "textures" : pathFromUtf8(gCacheDir);
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx", static_cast<unsigned long long>(fnv1a64(bytes, size)));
    const fs::path file = dir / (std::string("emb_") + name + ext);
    if (fs::exists(file, ec) && fs::file_size(file, ec) == size) return pathToUtf8(file);
    // Geçici dosyaya yaz, sonra taşı: yarım yazılmış doku başka bir içe aktarmaya görünmesin.
    const fs::path tmp = fs::path(file).concat(".tmp");
    {
        std::ofstream out(tmp, std::ios::binary);
        if (!out) return {};
        out.write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(size));
        if (!out) return {};
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return fs::exists(file) ? pathToUtf8(file) : std::string();
    }
    return pathToUtf8(file);
}

} // namespace photon
