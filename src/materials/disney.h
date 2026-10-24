// Disney Principled BRDF malzemesinin arayüzü: parametreler (metallic, roughness, specular,
// clearcoat, sheen, anizotropi, difüz geçirgenlik) ve doku haritaları. Matematik disney.cpp'de.
#pragma once

/// @file disney.h
/// @brief Simplified Disney Principled BRDF in PhotonEngine.

#include "materials/material.h"
#include "core/image/image.h"
#include "core/image/image_io.h"
#include <algorithm>
#include <memory>
#include <string>
#include <utility>

namespace photon {

/// @brief Simplified Disney Principled BRDF.
///
/// Combines a Burley diffuse lobe and a Cook-Torrance GGX specular microfacet lobe,
/// parameterized by metallic, roughness, and specular strength. Optional texture maps
/// (albedo / normal / roughness / metalness) are sampled at SurfaceInteraction::uv.
class DisneyMaterial : public Material {
public:
    DisneyMaterial(const Color3f& baseColor, float metallic, float roughness, float specular)
        : m_baseColor(baseColor), m_metallic(metallic), m_roughness(std::max(0.001f, roughness)), m_specular(specular) {}

    bool sample(const Vec3f& wo, const SurfaceInteraction& si, const Vec2f& sample,
                Vec3f& wi, Color3f& brdf, float& pdf) const override;

    Color3f eval(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const override;

    float pdf(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const override;

    Color3f evalPdf(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si, float& pdfOut) const override;

    /// Lob seçimi @p uc ile, yön @p u ile. Yüzey altı lobu seçilirse subsurface = true döner.
    bool sampleFull(const Vec3f& wo, const SurfaceInteraction& si, float uc, const Vec2f& u,
                    BSDFSample& out) const override;
    bool subsurfaceCoeffs(const SurfaceInteraction& si, SubsurfaceCoeffs& out) const override;

    const Color3f& baseColor() const { return m_baseColor; }
    float metallic() const { return m_metallic; }
    float roughness() const { return m_roughness; }
    float specular() const { return m_specular; }
    float clearCoat() const { return m_clearCoat; }
    float clearCoatRoughness() const { return m_clearCoatRoughness; }

    void setBaseColor(const Color3f& c) { m_baseColor = c; }
    void setMetallic(float v) { m_metallic = v; }
    void setRoughness(float v) { m_roughness = std::max(0.001f, v); }
    void setSpecular(float v) { m_specular = v; }
    void setClearCoat(float v) { m_clearCoat = std::clamp(v, 0.0f, 1.0f); }
    void setClearCoatRoughness(float v) { m_clearCoatRoughness = std::max(0.001f, v); }
    void setAnisotropy(float v) { m_anisotropy = std::clamp(v, 0.0f, 1.0f); }
    void setSheen(float v) { m_sheen = std::clamp(v, 0.0f, 1.0f); }
    /// Thin-surface diffuse transmission. Burley random-walk SSS does not fit this BRDF.
    void setDiffuseTransmission(float v) { m_diffuseTransmission = std::clamp(v, 0.0f, 1.0f); }
    void setEmission(const Color3f& e) { m_emission = e; }

    /// Yüzey altı saçılımı (Blender 4 Principled ile aynı anlam): difüz lobun bu oranı
    /// cismin İÇİNDE rastgele yürüyüşle taşınır (mum, yeşim, deri, sabun, süt, meyve).
    /// Renk taban rengidir; yarıçap kanal başına ortalama serbest yol (radius · scale,
    /// sahne birimi). Kırmızı ışık deride en uzağa gider: varsayılan (1, 0.2, 0.1).
    void setSubsurface(float v) { m_subsurface = std::clamp(v, 0.0f, 1.0f); }
    void setSubsurfaceRadius(const Color3f& r) { m_subsurfaceRadius = r.clamp(1e-4f, 1e4f); }
    void setSubsurfaceScale(float s) { m_subsurfaceScale = std::max(s, 1e-6f); }
    void setSubsurfaceAnisotropy(float g) { m_subsurfaceAnisotropy = std::clamp(g, -0.9f, 0.9f); }
    float subsurface() const { return m_subsurface; }
    const Color3f& subsurfaceRadius() const { return m_subsurfaceRadius; }
    float subsurfaceScale() const { return m_subsurfaceScale; }
    float subsurfaceAnisotropy() const { return m_subsurfaceAnisotropy; }

    /// Metal kenar tonu (F82-tint, OpenPBR "specular_color"): sıyırma açısında yansımanın
    /// rengi. Beyaz = Schlick; altın/bakırda gerçek ölçülmüş metallerin kenar renkleri.
    void setEdgeTint(const Color3f& c) { m_edgeTint = c.clamp(0.0f, 1.0f); }
    const Color3f& edgeTint() const { return m_edgeTint; }

    /// İnce film girişimi (sabun köpüğü, yağ tabakası, ısıl renk almış titanyum, mercek
    /// kaplaması): kalınlık nm cinsinden (0 = kapalı), filmin kırılma indisi.
    void setThinFilm(float thicknessNm, float ior) {
        m_thinFilmThickness = std::clamp(thicknessNm, 0.0f, 2000.0f);
        m_thinFilmIor = std::clamp(ior, 1.0f, 3.0f);
    }
    float thinFilmThickness() const { return m_thinFilmThickness; }
    float thinFilmIor() const { return m_thinFilmIor; }

    /// Metalik boya pulları (araç boyası): yoğunluk (0 = yok, 1 = her yer pul), pul boyu
    /// (sahne birimi), yön dağınıklığı (0 = hepsi yüzeye paralel) ve pul rengi.
    void setFlakes(float density, float size, float roughness, const Color3f& color) {
        m_flakeDensity = std::clamp(density, 0.0f, 1.0f);
        m_flakeSize = std::max(size, 1e-6f);
        m_flakeRoughness = std::clamp(roughness, 0.0f, 1.0f);
        m_flakeColor = color.clamp(0.0f, 1.0f);
    }
    float flakeDensity() const { return m_flakeDensity; }
    float flakeSize() const { return m_flakeSize; }
    float flakeRoughness() const { return m_flakeRoughness; }
    const Color3f& flakeColor() const { return m_flakeColor; }

    /// Doku koordinatı kaynağı. Box: UV'si olmayan modeller için dünya uzayında
    /// üç eksenden izdüşüm (yüzey normalinin baskın eksenine göre).
    enum class TextureMapping { UV, Box };
    void setTextureMapping(TextureMapping m) { m_mapping = m; }
    TextureMapping textureMapping() const { return m_mapping; }
    /// Doku tekrarı: UV'de çarpan, kutu eşlemede 1 dünya biriminde kaç tekrar.
    void setTextureScale(float s) { m_texScale = s > 1e-4f ? s : 1e-4f; }
    float textureScale() const { return m_texScale; }
    float anisotropy() const { return m_anisotropy; }
    float sheen() const { return m_sheen; }
    float diffuseTransmission() const { return m_diffuseTransmission; }
    void setAlbedoImage(std::shared_ptr<Image> image) { m_albedoTex = std::move(image); }

    Color3f emitted(const SurfaceInteraction&) const override { return m_emission; }

    const std::string& albedoMap() const { return m_albedoMap; }
    const std::string& normalMap() const { return m_normalMap; }
    const std::string& roughnessMap() const { return m_roughnessMap; }
    const std::string& metalnessMap() const { return m_metalnessMap; }
    void setAlbedoMap(const std::string& p);
    void setNormalMap(const std::string& p);
    void setRoughnessMap(const std::string& p);
    void setMetalnessMap(const std::string& p);

    /// Resolved scalar/texture parameters at a shading point (for tests / debug).
    struct ShadingParams {
        Color3f baseColor;
        float metallic = 0.0f;
        float roughness = 0.5f;
        float specular = 0.5f;
        float clearCoat = 0.0f;
        float clearCoatRoughness = 0.03f;
        float anisotropy = 0.0f;
        float sheen = 0.0f;
        float diffuseTransmission = 0.0f;
        float subsurface = 0.0f;
        Vec3f normal{0, 1, 0};
        Vec3f tangent{1, 0, 0};
    };
    ShadingParams resolve(const SurfaceInteraction& si) const;

private:
    static std::shared_ptr<Image> loadMap(const std::string& path, TextureEncoding encoding);

    Color3f m_baseColor;
    float m_metallic;
    float m_roughness;
    float m_specular;
    float m_clearCoat = 0.0f;
    float m_clearCoatRoughness = 0.03f;
    float m_anisotropy = 0.0f;
    float m_sheen = 0.0f;
    float m_diffuseTransmission = 0.0f;
    float m_subsurface = 0.0f;
    Color3f m_subsurfaceRadius{1.0f, 0.2f, 0.1f};
    float m_subsurfaceScale = 0.05f;
    float m_subsurfaceAnisotropy = 0.0f;
    Color3f m_edgeTint{1.0f};
    float m_thinFilmThickness = 0.0f;
    float m_thinFilmIor = 1.4f;
    float m_flakeDensity = 0.0f;
    float m_flakeSize = 0.004f;
    float m_flakeRoughness = 0.3f;
    Color3f m_flakeColor{0.9f};
    Color3f m_emission = Color3f::black();
    std::string m_albedoMap;
    std::string m_normalMap;
    std::string m_roughnessMap;
    std::string m_metalnessMap;
    std::shared_ptr<Image> m_albedoTex;
    std::shared_ptr<Image> m_normalTex;
    std::shared_ptr<Image> m_roughnessTex;
    std::shared_ptr<Image> m_metalnessTex;
    TextureMapping m_mapping = TextureMapping::UV;
    float m_texScale = 1.0f;

    Vec2f textureCoord(const SurfaceInteraction& si) const;
};

} // namespace photon
