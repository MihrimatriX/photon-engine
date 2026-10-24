// material_library.h — Hazır malzeme kütüphanesi (assets/materials/*.json).
//
// Her JSON dosyası bir "preset"tir: ad, kategori, tür (genel/cam) ve fiziksel
// parametreler. Kütüphane panelinde küçük resimleriyle listelenir; bir parçaya
// sürüklenince createMaterial() ile gerçek bir Material nesnesine dönüşür.
#pragma once

#include "materials/disney.h"
#include "materials/material.h"
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace photon {

/// Malzemenin hangi BSDF sınıfıyla oluşturulacağı.
enum class MaterialKind {
    Generic, ///< Disney Principled: plastik, metal, boya, kumaş, ışık yayan...
    Glass,   ///< Dielectric: cam, su, kristal (kırılma + Fresnel)
};

struct MaterialPreset {
    std::string id;
    std::string name;
    std::string category;
    std::string file;      ///< Yüklendiği JSON (boş = bellekte oluşturuldu)
    MaterialKind kind = MaterialKind::Generic;
    Color3f baseColor{0.8f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float specular = 0.5f;
    float clearCoat = 0.0f;
    float clearCoatRoughness = 0.03f;
    float anisotropy = 0.0f;
    float sheen = 0.0f;
    float diffuseTransmission = 0.0f;
    float emissive = 0.0f;  ///< Işık yayma gücü (baseColor ile çarpılır)
    float ior = 1.5f;       ///< Yalnız cam: kırılma indisi
    std::string albedoMap;  ///< Mutlak yol (JSON'da assets/textures'a göreli)
    std::string normalMap;
    std::string roughnessMap;
    bool boxMapping = false; ///< UV yerine kutu (triplanar) eşleme
    float texScale = 1.0f;

    // ── Gelişmiş: cam hacmi ve dispersiyon (yalnız cam) ──
    Color3f absorbColor{1.0f};     ///< absorbDistance kadar yol alınca ulaşılan renk
    float absorbDistance = 1.0f;   ///< sahne birimi
    float scatterDensity = 0.0f;   ///< bulanıklık (1/birim); 0 = berrak
    Color3f scatterColor{1.0f};
    float scatterAnisotropy = 0.0f;
    float abbe = 0.0f;             ///< 0 = dispersiyon yok

    // ── Gelişmiş: Principled ──
    float subsurface = 0.0f;
    Color3f subsurfaceRadius{1.0f, 0.2f, 0.1f};
    float subsurfaceScale = 0.05f;
    float subsurfaceAnisotropy = 0.0f;
    Color3f edgeTint{1.0f};        ///< metal: sıyırma açısındaki renk (F82 tonu)
    float thinFilmThickness = 0.0f;///< nm; 0 = ince film yok
    float thinFilmIor = 1.4f;
    float flakeDensity = 0.0f;     ///< metalik boya pulları; 0 = yok
    float flakeSize = 0.004f;      ///< sahne birimi
    float flakeRoughness = 0.3f;   ///< pul yönlerinin dağınıklığı
    Color3f flakeColor{0.9f};
    float roundEdges = 0.0f;       ///< yuvarlatılmış kenar yarıçapı (her tür)
    bool thinWalled = false;       ///< cam: ince duvar (pencere, köpük); film kalınlığı thinFilmThickness
};

/// Gelişmiş alanları JSON'a yaz / JSON'dan oku (yalnız varsayılandan farklı olanlar yazılır).
/// Preset dosyaları ve proje dosyası aynı anahtarları kullanır.
void presetAdvancedToJson(const MaterialPreset& p, nlohmann::json& j);
void presetAdvancedFromJson(const nlohmann::json& j, MaterialPreset& p);

/// Tüm parametrelerin kararlı metin özeti (küçük resim önbelleği, değişiklik algılama).
std::string presetSignature(const MaterialPreset& p);

class MaterialLibrary {
public:
    bool loadFromDirectory(const std::string& path);
    const std::vector<MaterialPreset>& presets() const { return m_presets; }
    const MaterialPreset* findById(const std::string& id) const;
    void add(MaterialPreset p) { m_presets.push_back(std::move(p)); }
    std::vector<std::string> categories() const;
    std::vector<const MaterialPreset*> byCategory(const std::string& cat) const;

    /// Preset'ten yeni, bağımsız bir Material nesnesi.
    static std::shared_ptr<Material> createMaterial(const MaterialPreset& p);

    /// Preset'in gelişmiş alanlarını (SSS, hacim, dispersiyon, kenar tonu, ince film,
    /// pullar) var olan bir malzemeye uygula. Tür uyuşmayan alanlar yok sayılır.
    static void applyAdvanced(const MaterialPreset& p, Material& m);

    /// Var olan bir malzemenin parametrelerini preset'e çevir (kütüphaneye kaydetmek için).
    static MaterialPreset presetFromMaterial(const Material& m, const std::string& name);

    /// Preset'i @p dir içine JSON olarak yaz ve listeye ekle. Dosya adı id'den türetilir.
    bool savePreset(MaterialPreset p, const std::string& dir);

private:
    std::vector<MaterialPreset> m_presets;
    bool loadFile(const std::string& path);
};

/// Malzemenin derin kopyası: render sahnesi kullanıcının düzenlediği nesneden
/// bağımsız olsun diye (son render ve turntable). Dokular paylaşılır (değişmezler).
std::shared_ptr<Material> cloneMaterial(const Material& m);

} // namespace photon
