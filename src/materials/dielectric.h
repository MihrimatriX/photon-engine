// Cam / şeffaf dielektrik malzeme. Pürüzsüz (roughness ≈ 0) hali delta yansıma +
// kırılma; pürüzlü hali GGX mikro-yüzey BSDF'si (PBRT-v4 DielectricBxDF, Walter 2007).
#pragma once

/// @file dielectric.h
/// @brief Dielectric (glass/transparent) material in PhotonEngine.

#include "materials/material.h"

#include <algorithm>

namespace photon {

/// @brief Glass-like dielectric refraction and reflection material.
/// Roughness 0 is a delta surface. Roughness > 0 is a GGX microfacet BSDF.
class Dielectric : public Material {
public:
    Dielectric(float ior, const Color3f& tint, float roughness = 0.0f)
        : m_ior(ior), m_tint(tint), m_roughness(roughness) {}

    /// Eski 2B arayüz: lob seçimi için uc = sample.x kullanılır (bkz. sampleWithLobe).
    bool sample(const Vec3f& wo, const SurfaceInteraction& si, const Vec2f& sample,
                Vec3f& wi, Color3f& brdf, float& pdf) const override;

    bool sampleWithLobe(const Vec3f& wo, const SurfaceInteraction& si, float uc, const Vec2f& u,
                        Vec3f& wi, Color3f& brdf, float& pdf) const override;

    Color3f eval(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const override;

    float pdf(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const override;
    /// Pürüzlü cam: yansıma parlak, kırılma geçirgen.
    LobeSplit evalSplit(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const override;

    float ior() const { return m_ior; }
    const Color3f& tint() const { return m_tint; }
    float roughness() const { return m_roughness; }
    bool isDelta() const override { return m_thinWalled || m_roughness <= 1e-3f; } // dielectric.cpp: sharp()

    // ── İnce duvar ve ince film ─────────────────────────────────────────────
    /// İnce duvarlı: yüzey kalınlığı olmayan bir zar (pencere camı, sabun köpüğü, ampul).
    /// Işık kırılmadan geçer (iki yüzün kırılması birbirini sıfırlar), içerde ortam yok,
    /// gölge ışınları da renkli olarak geçer. Pürüzlülük yok sayılır.
    void setThinWalled(bool v) { m_thinWalled = v; }
    bool thinWalled() const { return m_thinWalled; }
    /// İnce duvarın kendisi bir girişim filmi olsun (sabun köpüğü): kalınlık nm, 0 = yok.
    void setThinFilm(float thicknessNm) { m_filmThickness = thicknessNm > 0.0f ? thicknessNm : 0.0f; }
    float thinFilmThickness() const { return m_filmThickness; }
    bool transmitsShadows() const override { return m_thinWalled; }
    Color3f shadowTransmittance(const SurfaceInteraction& si, const Vec3f& dir) const override;
    bool sampleFull(const Vec3f& wo, const SurfaceInteraction& si, float uc, const Vec2f& u,
                    BSDFSample& out) const override;

    // ── Hacim: renkli cam/sıvı soğurması ve bulanıklık ──────────────────────
    /// Beer–Lambert soğurması, sanatçı dostu biçimde: ışık camın içinde @p distance
    /// (sahne birimi) yol alınca rengi @p color olur. σa = −ln(color) / distance.
    /// Kalın yerler koyu, ince yerler açık görünür (gerçek renkli cam, şarap, bal).
    /// color = beyaz → soğurma yok.
    void setAbsorption(const Color3f& color, float distance);
    /// Bulanıklık (süt, sabunlu su, opal): yoğunluk ρ (1/birim), saçılma rengi C (tek
    /// saçılma albedosu), anizotropi g. σs = ρ·C, σa += ρ·(1 − C).
    void setScattering(float density, const Color3f& color, float anisotropy);
    const Color3f& absorptionColor() const { return m_absorbColor; }
    float absorptionDistance() const { return m_absorbDistance; }
    float scatterDensity() const { return m_scatterDensity; }
    const Color3f& scatterColor() const { return m_scatterColor; }
    float scatterAnisotropy() const { return m_scatterAniso; }

    bool isVolumeBoundary() const override { return !m_thinWalled; }
    const Medium* interiorMedium() const override {
        return (m_thinWalled || m_medium.empty()) ? nullptr : &m_medium;
    }

    // ── Dispersiyon ─────────────────────────────────────────────────────────
    /// Abbe sayısı (0 = dispersiyon yok). Küçüldükçe beyaz ışık daha çok renge ayrılır.
    /// Gerçek camlarda V ≈ 17–95; 10'un altı Cauchy yaklaşımında kırmızıda n < 1 verir.
    void setAbbe(float abbe) { m_abbe = abbe > 0.0f ? std::max(abbe, 10.0f) : 0.0f; }
    float abbe() const { return m_abbe; }
    bool isDispersive() const override { return m_abbe > 0.0f; }
    /// Bu kesişimde geçerli kırılma indisi (yolun dalga boyuna göre).
    float iorAt(const SurfaceInteraction& si) const;

private:
    void rebuildMedium();

    float m_ior;
    Color3f m_tint;
    float m_roughness = 0.0f;
    float m_abbe = 0.0f;
    Color3f m_absorbColor{1.0f};
    float m_absorbDistance = 1.0f;
    float m_scatterDensity = 0.0f;
    Color3f m_scatterColor{1.0f};
    float m_scatterAniso = 0.0f;
    Medium m_medium;
    bool m_thinWalled = false;
    float m_filmThickness = 0.0f;

    /// İnce duvarın yansıtması (kanal başına): girişim filmi ya da iki yüzlü levha.
    Color3f thinWallReflectance(float cosTheta) const;
};

} // namespace photon
