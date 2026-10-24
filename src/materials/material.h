// Malzeme (BSDF) arayüzü: sample / eval / pdf / emitted. Tüm malzemeler bunu uygular.
// Yön kuralı PBRT ile aynı: wo ve wi yüzeyden DIŞARI bakar, birim uzunluktadır.
#pragma once

/// @file material.h
/// @brief Abstract Material base class in PhotonEngine.

#include "core/color/spectrum.h"
#include "core/math/vec.h"
#include "geometry/surface_interaction.h"

namespace photon {

/// Homojen katılımcı ortam (participating medium): bir cismin İÇİNİ dolduran madde.
/// σa soğurma, σs saçılma katsayısıdır (1/sahne birimi); toplam sönüm σt = σa + σs.
/// Işın ortamda d kadar yol alınca enerjisinin exp(−σt·d)'si kalır (Beer–Lambert).
/// g: Henyey–Greenstein anizotropisi (0 = her yöne, > 0 = ileri saçılma).
struct Medium {
    Color3f sigmaA;
    Color3f sigmaS;
    float g = 0.0f;

    Color3f sigmaT() const { return sigmaA + sigmaS; }
    bool scatters() const { return !sigmaS.isBlack(); }
    bool empty() const { return sigmaA.isBlack() && sigmaS.isBlack(); }
};

/// Rastgele yürüyüş yüzey altı saçılımının (SSS) bir yüzey noktasındaki katsayıları
/// (dokular çözülmüş). albedo = tek saçılma albedosu σs/σt.
struct SubsurfaceCoeffs {
    Color3f sigmaT;
    Color3f albedo;
    float g = 0.0f;
};

/// BSDF örneklemesinin sonucu (PBRT-v4 BSDFSample'ın sadeleşmiş hali).
struct BSDFSample {
    Vec3f wi;
    Color3f f;
    float pdf = 0.0f;
    bool delta = false;      ///< Dirac lobu (ayna, pürüzsüz cam): NEE ve MIS yok.
    /// Yüzey altına giriş: integratör cismin içinde rastgele yürüyüş yapar, ışık başka
    /// bir noktadan çıkar. Bu durumda f/pdf yerine doğrudan 'weight' kullanılır.
    bool subsurface = false;
    Color3f weight;          ///< subsurface = true iken giriş ağırlığı (f·|cos|/pdf)
};

/// @brief Abstract class representing a surface material/BSDF.
///
/// Uses standard PBRT conventions: all vectors wo and wi are normalized
/// and point AWAY from the surface interaction point in the local coordinate frame.
class Material {
public:
    virtual ~Material() = default;

    /// @brief Sample an incident direction wi given an outgoing direction wo.
    ///
    /// @param[in]  wo       Outgoing direction (points toward viewer, away from surface).
    /// @param[in]  si       Surface interaction details.
    /// @param[in]  sample   2D random sample in [0, 1)².
    /// @param[out] wi       Sampled incident direction (points toward light/next bounce, away from surface).
    /// @param[out] brdf     Evaluated BRDF/BSDF value for (wo, wi).
    /// @param[out] pdf      PDF value of sampling wi.
    /// @return true if sampling succeeded, false if invalid.
    virtual bool sample(const Vec3f& wo, const SurfaceInteraction& si, const Vec2f& sample,
                        Vec3f& wi, Color3f& brdf, float& pdf) const = 0;

    /// @brief sample() ile aynı, ama lob seçimi için ayrı bir 1B örnek @p uc alır.
    ///
    /// Neden: Kaba camda (PBRT-v4 DielectricBxDF) önce mikro-normal h iki boyutlu
    /// @p u ile örneklenir, SONRA yansıma/kırılma kararı F(wo·h) olasılığıyla verilir.
    /// Bu karar için u'dan bağımsız üçüncü bir sayı gerekir; u.x'i yeniden kullanmak
    /// h ile kararı ilişkilendirir. Varsayılan uygulama uc'yi yok sayar, yani bu ek
    /// arayüze ihtiyaç duymayan malzemeler değişmeden çalışır. Integratör bunu çağırır.
    virtual bool sampleWithLobe(const Vec3f& wo, const SurfaceInteraction& si, float /*uc*/,
                                const Vec2f& u, Vec3f& wi, Color3f& brdf, float& pdf) const {
        return sample(wo, si, u, wi, brdf, pdf);
    }

    /// @brief Evaluate the BRDF/BSDF for a given pair of directions (wo, wi).
    virtual Color3f eval(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const = 0;

    /// @brief Evaluate the probability density function for sampling wi.
    virtual float pdf(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const = 0;

    /// sampleWithLobe'un zengin hali: lob bayraklarını da döner. Integratör bunu çağırır.
    /// Varsayılan uygulama sampleWithLobe + isDelta()'dır.
    virtual bool sampleFull(const Vec3f& wo, const SurfaceInteraction& si, float uc, const Vec2f& u,
                            BSDFSample& out) const {
        out.delta = isDelta();
        out.subsurface = false;
        return sampleWithLobe(wo, si, uc, u, out.wi, out.f, out.pdf);
    }

    /// Kapalı bir hacmin sınırı mı (katı cam, sıvı)? Öyleyse içinden geçen (kırılan) ışın
    /// ortam değiştirir ve integratör interiorMedium()'u izler. İnce yüzeyler (difüz
    /// geçirgen yaprak) false döner: ışın geçer ama "içeride" kalınmaz.
    virtual bool isVolumeBoundary() const { return false; }

    /// Cismin içindeki ortam (renkli cam soğurması, bulanık sıvı). nullptr = boşluk.
    virtual const Medium* interiorMedium() const { return nullptr; }

    /// Kırılma indisi dalga boyuna bağlı mı (dispersiyon)? Öyleyse integratör yol için
    /// bir dalga boyu seçer ve SurfaceInteraction::wavelength'i doldurur.
    virtual bool isDispersive() const { return false; }

    /// sampleFull 'subsurface' döndürdüğünde rastgele yürüyüşün katsayıları.
    virtual bool subsurfaceCoeffs(const SurfaceInteraction&, SubsurfaceCoeffs&) const { return false; }

    /// Yalnız delta (Dirac) lobları varsa true: kusursuz ayna, pürüzsüz cam. Bu yüzeylerde
    /// ışık örneklemesi (NEE) anlamsızdır (BSDF tek bir yön dışında 0) ve bir sonraki
    /// isabetteki ışık yayımı MIS ağırlığı olmadan eklenir. Eskiden integratör bunu
    /// eval() sonucunun siyah olmasından çıkarıyordu: köşe başına iki gereksiz eval.
    virtual bool isDelta() const { return false; }

    /// eval + pdf tek çağrıda. Doku okuma gibi parametre çözümleme işi olan malzemeler
    /// (Disney) bunu bir kez yapmak için üzerine yazar.
    virtual Color3f evalPdf(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si, float& pdfOut) const {
        pdfOut = pdf(wo, wi, si);
        return eval(wo, wi, si);
    }

    /// @brief Returns emitted radiance from the surface (for light sources).
    virtual Color3f emitted(const SurfaceInteraction&) const {
        return Color3f::black();
    }
};

} // namespace photon
