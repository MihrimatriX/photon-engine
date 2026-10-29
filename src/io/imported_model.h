// imported_model.h — Dış biçimlerden (FBX, Alembic, ...) içe aktarılan sahnenin ortak, motordan
// bağımsız gösterimi ve yükleyicilerin paylaştığı yardımcılar.
//
// Hiyerarşi korunur: her düğümün adı ve ebeveynine göre yerel dönüşümü vardır, mesh'ler düğümün
// YEREL uzayındadır (dünya uzayına gömülmez). Aynı mesh'i kullanan düğümler (örnekleme/instancing)
// aynı TriangleMesh'i paylaşır. meshes[i]'nin sırası dosyadaki kararlı gezinme sırasıdır; proje
// dosyası parçaları bu sırayla (sourceIndex) eşler.
#pragma once

#include "core/math/transform.h"
#include "geometry/mesh.h"
#include "materials/material.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace photon {

struct ImportedMesh {
    std::shared_ptr<TriangleMesh> mesh;
    std::shared_ptr<Material> material; ///< nullptr → çağıran varsayılan malzemeyi verir
    std::string name;                   ///< parça adı (düğüm adı, malzeme dilimi varsa "düğüm · malzeme")
};

struct ImportedNode {
    std::string name;
    Transform local;          ///< ebeveyne göre
    std::vector<int> meshes;  ///< ImportedModel::meshes sırası
    std::vector<ImportedNode> children;
};

struct ImportedModel {
    std::vector<ImportedMesh> meshes;
    ImportedNode root;
    std::vector<std::string> warnings;
    bool empty() const { return meshes.empty(); }
};

/// Üçgen köşelerini (konum, normal, UV) toplar; aynı köşeleri birleştirerek indeksli bir mesh kurar.
/// Yükleyiciler köşe başına öznitelikleri (face-varying) okur; birleştirme olmadan her üçgen köşesi
/// ayrı bir vertex olurdu (bellek 3–6 kat).
class MeshBuilder {
public:
    void addCorner(const Vec3f& p, const Vec3f& n, const Vec2f& uv);
    size_t triangleCount() const { return m_indices.size() / 3; }
    /// Normaller yoksa (hepsi sıfır) TriangleMesh kendi ortalama normallerini hesaplar.
    std::shared_ptr<TriangleMesh> build(const Material* material);

private:
    struct Key {
        uint32_t v[8];
        bool operator==(const Key& o) const;
    };
    struct KeyHash {
        size_t operator()(const Key& k) const;
    };
    std::vector<Vec3f> m_p, m_n;
    std::vector<Vec2f> m_uv;
    std::vector<uint32_t> m_indices;
    std::unordered_map<Key, uint32_t, KeyHash> m_lookup;
    bool m_hasNormals = false;
};

/// Dosyadaki normaller üçgenlerin sarma yönüyle (sağ el kuralı) çelişiyorsa — çoğu üçgenin
/// geometrik normali köşe normallerinin tersine bakıyorsa — tüm üçgenlerin sırasını çevirir.
/// Cam giriş/çıkışı geometrik normale dayandığı için sarma yönü önemlidir. Biçimlerin kuralları
/// farklıdır (Alembic saat yönünde, FBX/glTF saat yönü tersine) ve dışa aktarıcılar her zaman uymaz.
/// @return çevrildiyse true.
bool orientWindingToNormals(const std::vector<Vec3f>& positions, const std::vector<Vec3f>& normals,
                            std::vector<uint32_t>& indices);

/// Model dosyasına gömülü doku verisini (png/jpg/... baytları) kullanıcı veri klasöründeki doku
/// önbelleğine içerik karmasıyla yazar ve yolunu döner (zaten varsa yeniden yazmaz). Doku böylece
/// bir dosya yolu olur: malzeme paneli, proje dosyası ve yeniden açma onu normal doku gibi görür.
/// Tanınmayan biçim ya da yazma hatasında boş döner.
std::string cacheEmbeddedTexture(const void* data, size_t size);

/// Gömülü doku önbelleğinin klasörü (testler başka yere yönlendirebilir; boş = varsayılan).
void setEmbeddedTextureCacheDir(const std::string& dir);

} // namespace photon
