// mesh_binary.cpp — .mesh kodlama/çözme.
#include "scene/mesh_binary.h"
#include "core/platform/path.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace photon {

static_assert(std::endian::native == std::endian::little, ".mesh biçimi küçük-sonlu bayt sırası varsayar");

namespace {

constexpr char kMagic[8] = {'P', 'H', 'M', 'E', 'S', 'H', '0', '1'};
constexpr uint32_t kHasNormals = 1u, kHasUvs = 2u;
constexpr size_t kHeader = 24;

template <typename T>
void append(std::vector<uint8_t>& out, const T* data, size_t count) {
    const size_t bytes = count * sizeof(T);
    const size_t at = out.size();
    out.resize(at + bytes);
    if (bytes) std::memcpy(out.data() + at, data, bytes);
}

std::shared_ptr<TriangleMesh> bad(std::string* error, const char* msg) {
    if (error) *error = msg;
    return nullptr;
}

} // namespace

std::vector<uint8_t> encodeMeshBinary(const TriangleMesh& mesh) {
    const auto& p = mesh.positions();
    const auto& n = mesh.normals();
    const auto& uv = mesh.uvs();
    const auto& idx = mesh.indices();
    const uint32_t vc = static_cast<uint32_t>(p.size());
    const uint32_t flags = (n.size() == p.size() && !n.empty() ? kHasNormals : 0u) |
                           (uv.size() == p.size() && !uv.empty() ? kHasUvs : 0u);
    std::vector<uint8_t> out;
    out.reserve(kHeader + p.size() * 32 + idx.size() * 4);
    append(out, kMagic, 8);
    const uint32_t head[4] = {vc, static_cast<uint32_t>(idx.size()), flags, 0u};
    append(out, head, 4);
    for (const Vec3f& v : p) append(out, &v.x, 3);
    if (flags & kHasNormals)
        for (const Vec3f& v : n) append(out, &v.x, 3);
    if (flags & kHasUvs)
        for (const Vec2f& v : uv) append(out, &v.x, 2);
    append(out, idx.data(), idx.size());
    return out;
}

std::shared_ptr<TriangleMesh> decodeMeshBinary(const uint8_t* data, size_t size, const Material* material,
                                               std::string* error) {
    if (size < kHeader || std::memcmp(data, kMagic, 8) != 0) return bad(error, "Geçersiz mesh dosyası");
    uint32_t head[4];
    std::memcpy(head, data + 8, sizeof(head));
    const uint64_t vc = head[0], ic = head[1];
    const uint32_t flags = head[2];
    const uint64_t expect = kHeader + vc * 12 + ((flags & kHasNormals) ? vc * 12 : 0) +
                            ((flags & kHasUvs) ? vc * 8 : 0) + ic * 4;
    if (vc == 0 || ic == 0 || ic % 3 != 0 || expect != size) return bad(error, "Bozuk mesh dosyası (boyut)");
    const uint8_t* q = data + kHeader;
    std::vector<Vec3f> p(static_cast<size_t>(vc)), n;
    std::vector<Vec2f> uv;
    std::vector<uint32_t> idx(static_cast<size_t>(ic));
    for (Vec3f& v : p) {
        std::memcpy(&v.x, q, 12);
        q += 12;
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
            return bad(error, "Bozuk mesh dosyası (sonlu olmayan konum)");
    }
    if (flags & kHasNormals) {
        n.resize(static_cast<size_t>(vc));
        for (Vec3f& v : n) {
            std::memcpy(&v.x, q, 12);
            q += 12;
        }
    }
    if (flags & kHasUvs) {
        uv.resize(static_cast<size_t>(vc));
        for (Vec2f& v : uv) {
            std::memcpy(&v.x, q, 8);
            q += 8;
        }
    }
    std::memcpy(idx.data(), q, idx.size() * 4);
    for (uint32_t i : idx)
        if (i >= vc) return bad(error, "Bozuk mesh dosyası (sınır dışı indeks)");
    return std::make_shared<TriangleMesh>(p, n, uv, idx, material);
}

std::shared_ptr<TriangleMesh> readMeshBinaryFile(const std::string& path, const Material* material, std::string* error) {
    std::ifstream in(pathFromUtf8(path), std::ios::binary);
    if (!in) return bad(error, "Mesh dosyası açılamadı");
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return decodeMeshBinary(bytes.data(), bytes.size(), material, error);
}

} // namespace photon
