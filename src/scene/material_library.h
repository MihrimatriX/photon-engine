// material_library.h — Hazır malzeme kütüphanesi (assets/materials/*.json).
//
// Her JSON dosyası bir "preset"tir: ad, kategori, tür (genel/cam) ve fiziksel
// parametreler. Kütüphane panelinde küçük resimleriyle listelenir; bir parçaya
// sürüklenince createMaterial() ile gerçek bir Material nesnesine dönüşür.
#pragma once

#include "materials/disney.h"
#include "materials/material.h"
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
};

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
