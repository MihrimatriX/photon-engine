// Cam malzemesinin uygulaması. Pürüzsüz cam: Fresnel olasılığıyla delta yansıma ya da
// Snell kırılması. Pürüzlü cam: PBRT-v4 DielectricBxDF'nin birebir karşılığı
// (genelleştirilmiş yarı vektör, mikro-yüzey TIR, Walter 2007 Jacobian'ı, VNDF örnekleme).
// Hepsi yerel gölgeleme çerçevesinde hesaplanır: +z = dışarı bakan gölgeleme normali.

#include "materials/dielectric.h"
#include "materials/microfacet.h"
#include "core/math/frame.h"
#include "core/math/utils.h"
#include <algorithm>
#include <cmath>

namespace photon {

namespace {

bool sharp(float roughness) { return roughness <= 1e-3f; }

float alphaOf(float roughness) { return ggxClampAlpha(roughness * roughness); }

struct GlassLocal {
    Frame frame;
    Vec3f wo; ///< wo yerel çerçevede; wo.z > 0 ⇔ ışın dışarıdan geliyor (camera giriyor)
};

/// Cam için yerel çerçeve.
///
/// - ng (geometrik, DIŞA dönük) yalnızca "içeride miyiz, dışarıda mıyız" sorusunu
///   cevaplar; SurfaceInteraction::normal ise ışına doğru çevrildiği için bunu bilemez.
/// - Çerçeve, ng yarıküresine çevrilmiş GÖLGELEME normali ns ile kurulur; böylece
///   yumuşak gölgelenmiş cam modeller yüz yüz (faceted) görünmez (bulgu materials-8)
///   ve integratörün |wi·ns| kosinüsü delta lobun 1/|cosθ| değeriyle tam sadeleşir.
/// - Silüette wo, ns ve ng'ye göre farklı taraflara düşebilir. O zaman "içeride mi"
///   cevabı ile çerçeve çelişir; bu nadir durumda çerçeveyi ng ile kurarız.
GlassLocal glassFrame(const Vec3f& woWorld, const SurfaceInteraction& si) {
    Vec3f outward = si.frontFace ? si.normal : -si.normal;
    Vec3f ng = si.ng.lengthSquared() > 1e-12f ? si.ng.normalized() : outward.normalized();
    Vec3f ns = si.normal.lengthSquared() > 1e-12f ? si.normal.normalized() : ng;
    if (ns.dot(ng) < 0.0f) ns = -ns;
    if (woWorld.dot(ns) * woWorld.dot(ng) <= 0.0f) ns = ng;
    GlassLocal g{Frame(ns), Vec3f(0.0f)};
    g.wo = g.frame.toLocal(woWorld);
    return g;
}

/// Genelleştirilmiş yarı vektör (Walter 2007, PBRT-v4 §9.7.4).
///
/// Yansımada h = normalize(wo + wi). Kırılmada Snell'den  ηo·wo + ηi·wi ∥ h  gelir;
/// wo tarafının kırılma indisine bölünce  h ∝ wo + etap·wi,  etap = ηi/ηo
/// (wo dışarıdaysa etap = ior, içerideyse 1/ior). h'yi +z'ye çeviririz (mikro-normaller
/// hep dışa bakar), sonra wo ya da wi'nin "arkadan gördüğü" mikro-yüzeyleri atarız.
/// false → bu (wo, wi) çifti için BSDF ve pdf sıfır.
bool generalizedHalf(const Vec3f& wo, const Vec3f& wi, float ior, Vec3f& wm, float& etap) {
    float cosO = wo.z;
    float cosI = wi.z;
    if (cosO == 0.0f || cosI == 0.0f) return false;
    bool reflect = cosO * cosI > 0.0f;
    etap = reflect ? 1.0f : (cosO > 0.0f ? ior : 1.0f / ior);
    wm = wi * etap + wo;
    if (wm.lengthSquared() <= 1e-12f) return false;
    wm = wm.normalized();
    if (wm.z < 0.0f) wm = -wm;
    // Arkası dönük mikro-yüzey: h, wo ya da wi'yi kendi yarıküresinin tersinden görüyor.
    if (wm.dot(wi) * cosI <= 0.0f || wm.dot(wo) * cosO <= 0.0f) return false;
    return true;
}

/// Pürüzlü cam BSDF değeri (PBRT-v4 DielectricBxDF::f, radiance modu).
///
///   yansıma:  f_r = D(h)·G·F / |4 cosθi cosθo|
///   kırılma:  f_t = D(h)·G·(1-F) · |(wi·h)(wo·h)| / (|cosθi cosθo| (wi·h + (wo·h)/etap)²)  / etap²
///
/// F = F(wo·h) +z yönlü h ile hesaplanır: wo camın İÇİNDEyse wo·h < 0 olur ve
/// fresnelDielectric indisleri kendisi değiştirir; mikro-yüzey üzerinde tam iç yansıma
/// (TIR) böylece F = 1 olarak doğal biçimde çıkar (bulgu materials-2).
/// Son 1/etap² terimi radyansın n² ile ölçeklenmesidir: L/n² korunur (Veach 1997 §5.2).
Color3f evalRoughLocal(const Vec3f& wo, const Vec3f& wi, float ior, float alpha, const Color3f& tint) {
    Vec3f wm;
    float etap = 1.0f;
    if (!generalizedHalf(wo, wi, ior, wm, etap)) return Color3f::black();
    float cosO = wo.z;
    float cosI = wi.z;
    float woDotH = wo.dot(wm);
    float wiDotH = wi.dot(wm);
    float F = fresnelDielectric(woDotH, 1.0f, ior);
    float D = ggxD(wm, alpha, alpha);
    float G = ggxG(wo, wi, alpha, alpha);
    if (cosO * cosI > 0.0f) {
        return tint * (D * G * F / std::abs(4.0f * cosI * cosO));
    }
    float s = wiDotH + woDotH / etap;
    float denom = s * s * cosI * cosO;
    if (std::abs(denom) <= 1e-12f) return Color3f::black();
    float ft = D * (1.0f - F) * G * std::abs(wiDotH * woDotH / denom);
    return tint * (ft / (etap * etap));
}

/// Pürüzlü camın örnekleme pdf'i (katı açı), sampleRoughLocal ile birebir aynı süreç:
///   h ~ D_wo(h) (görünür normaller), sonra R = F(wo·h) olasılığıyla yansıma.
///   yansıma:  pdf = D_wo(h) · 1/(4|wo·h|) · R
///   kırılma:  pdf = D_wo(h) · |wi·h| / (wi·h + (wo·h)/etap)² · (1-R)
/// İkinci satırdaki kesir dh/dwi Jacobian'ıdır (Walter 2007 denklem 17, etap ile).
float pdfRoughLocal(const Vec3f& wo, const Vec3f& wi, float ior, float alpha) {
    Vec3f wm;
    float etap = 1.0f;
    if (!generalizedHalf(wo, wi, ior, wm, etap)) return 0.0f;
    float woDotH = wo.dot(wm);
    float wiDotH = wi.dot(wm);
    float R = fresnelDielectric(woDotH, 1.0f, ior);
    float visible = ggxVisiblePdf(wo, wm, alpha, alpha);
    if (wo.z * wi.z > 0.0f) {
        return visible / (4.0f * std::abs(woDotH)) * R;
    }
    float s = wiDotH + woDotH / etap;
    if (s * s <= 1e-12f) return 0.0f;
    return visible * std::abs(wiDotH) / (s * s) * (1.0f - R);
}

} // namespace

bool Dielectric::sample(const Vec3f& wo, const SurfaceInteraction& si, const Vec2f& sample,
                        Vec3f& wi, Color3f& brdf, float& pdf) const {
    // ponytail: eski çağıranlar için uc = u.x; pürüzlü camda h ile lob kararı ilişkili olur.
    // Integratör sampleWithLobe'u ayrı bir uc ile çağırır.
    return sampleWithLobe(wo, si, sample.x, sample, wi, brdf, pdf);
}

bool Dielectric::sampleWithLobe(const Vec3f& woWorld, const SurfaceInteraction& si, float uc,
                                const Vec2f& u, Vec3f& wi, Color3f& brdf, float& pdf) const {
    GlassLocal g = glassFrame(woWorld, si);
    const Vec3f& wo = g.wo;
    if (wo.z == 0.0f) return false;
    const bool entering = wo.z > 0.0f;
    // refractVec kuralı: eta = ηi/ηt, ηi = wo'nun bulunduğu ortam.
    const float eta = entering ? (1.0f / m_ior) : m_ior;

    if (sharp(m_roughness)) {
        // Delta cam: olasılık F ile yansıma, 1-F ile kırılma. Değerler "ağırlık" olarak
        // döner: brdf·|cosθ|/pdf = F/F = 1 (yansıma) veya (1-F)·eta²/(1-F) (kırılma).
        float F = fresnelDielectric(wo.z, 1.0f, m_ior);
        Vec3f wiLocal(-wo.x, -wo.y, wo.z);
        if (uc < F) {
            pdf = F;
            brdf = m_tint * (F / std::abs(wiLocal.z));
        } else {
            Vec3f n(0.0f, 0.0f, entering ? 1.0f : -1.0f);
            // wo yüzeyden DIŞARI bakar ve n ile aynı taraftadır (core-1 düzeltmesi:
            // eskiden -wo veriliyordu ve kırılma teğet düzlemde aynalanıyordu).
            if (!refractVec(wo, n, eta, wiLocal)) {
                // TIR'de F = 1 olduğundan buraya yalnızca yuvarlama ile düşülür.
                wiLocal = Vec3f(-wo.x, -wo.y, wo.z);
                pdf = 1.0f;
                brdf = m_tint / std::abs(wiLocal.z);
            } else {
                pdf = 1.0f - F;
                // Radyans ölçeklemesi (materials-4): sınırda L/η² korunur, yani
                // L(wo) = (1-F)·L(wi)·(η_wo/η_wi)² = (1-F)·L(wi)·eta². Kamera ışını
                // cama girerken (wo havada) eta = 1/ior → 1/ior² (eskiden ior² idi).
                brdf = m_tint * ((1.0f - F) * eta * eta / std::abs(wiLocal.z));
            }
        }
        wi = g.frame.toWorld(wiLocal).normalized();
        return true;
    }

    // Pürüzlü cam (PBRT-v4 DielectricBxDF::Sample_f): önce wo'dan görünen bir mikro-normal
    // h seç, sonra o mikro-yüzeyin Fresnel'i R = F(wo·h) ile yansı ya da kır.
    const float alpha = alphaOf(m_roughness);
    Vec3f wm = ggxSampleVisibleNormal(wo, alpha, alpha, u.x, u.y);
    float R = fresnelDielectric(wo.dot(wm), 1.0f, m_ior);
    Vec3f wiLocal;
    if (uc < R) {
        wiLocal = reflectVec(wo, wm);
        if (wiLocal.z * wo.z <= 0.0f) return false;
    } else {
        Vec3f n = entering ? wm : -wm;
        if (!refractVec(wo, n, eta, wiLocal)) return false;
        if (wiLocal.z * wo.z >= 0.0f) return false;
    }
    // f ve pdf'i aynı fonksiyonlardan almak eval()/pdf() ile tutarlılığı garanti eder.
    brdf = evalRoughLocal(wo, wiLocal, m_ior, alpha, m_tint);
    pdf = pdfRoughLocal(wo, wiLocal, m_ior, alpha);
    wi = g.frame.toWorld(wiLocal).normalized();
    return pdf > 0.0f && !brdf.isBlack();
}

Color3f Dielectric::eval(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const {
    if (sharp(m_roughness)) return Color3f::black();
    GlassLocal g = glassFrame(wo, si);
    return evalRoughLocal(g.wo, g.frame.toLocal(wi), m_ior, alphaOf(m_roughness), m_tint);
}

float Dielectric::pdf(const Vec3f& wo, const Vec3f& wi, const SurfaceInteraction& si) const {
    if (sharp(m_roughness)) return 0.0f;
    GlassLocal g = glassFrame(wo, si);
    return pdfRoughLocal(g.wo, g.frame.toLocal(wi), m_ior, alphaOf(m_roughness));
}

} // namespace photon
