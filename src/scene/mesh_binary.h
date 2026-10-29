// mesh_binary.h — Üçgen mesh'in ikili biçimi (.mesh, .photonpack içinde).
//
// JSON'a sayı dizisi olarak gömmek büyük modelde dosyayı on kat şişirir ve okuması yavaştır; paket
// geometriyi bu küçük ikili biçimde taşır (DEFLATE ile ayrıca sıkıştırılır):
//   "PHMESH01" | u32 köşe | u32 indeks | u32 bayrak (1 normal, 2 UV) | u32 0
//   | f32 konum[3·köşe] | f32 normal[3·köşe]? | f32 uv[2·köşe]? | u32 indeks[indeks]
// Bayt sırası küçük-sonlu (x86, ARM). Okuyucu güvenilmeyen veriye karşı denetler: boyut tutarlı,
// indeksler sınır içinde, konumlar sonlu.
#pragma once

#include "geometry/mesh.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace photon {

std::vector<uint8_t> encodeMeshBinary(const TriangleMesh& mesh);
std::shared_ptr<TriangleMesh> decodeMeshBinary(const uint8_t* data, size_t size, const Material* material,
                                               std::string* error = nullptr);
std::shared_ptr<TriangleMesh> readMeshBinaryFile(const std::string& path, const Material* material,
                                                 std::string* error = nullptr);

} // namespace photon
