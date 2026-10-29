// Trowbridge-Reitz (GGX) mikro-yüzey yardımcıları: dağılım D, Smith gölgeleme G
// ve görünür normal (VNDF) örneklemesi. Disney ve Dielectric aynı kodu kullanır.
// Tüm vektörler yerel gölgeleme çerçevesindedir: +z = yüzey normali.
// Kaynaklar: Walter vd. 2007 (GGX), Heitz 2014 (Smith), Heitz 2018 (VNDF), PBRT-v4 §9.6.
#pragma once

#include "core/math/constants.h"
#include "core/math/vec.h"
#include <algorithm>
#include <cmath>

namespace photon {

/// Değerlendirme için alt sınır: alpha bundan küçükse D tepe değeri float'ı taşırır.
inline constexpr float GGX_MIN_ALPHA = 1e-4f;

inline float ggxClampAlpha(float a) { return std::max(a, GGX_MIN_ALPHA); }

/// GGX normal dağılımı D(h), anizotrop (ax, ay).
///
/// Klasik yazım D = a² / (π (cos²θ (a²-1) + 1)²) küçük a'da iptal (cancellation)
/// yüzünden payda 0'a düşer ve inf verir. Burada aynı ifadenin iptalsiz biçimi var:
///   D(h) = 1 / (π · ax · ay · (h.x²/ax² + h.y²/ay² + h.z²)²)
/// Parantez içi her zaman ≥ h.z² + ... > 0 olduğu için taşma/NaN olmaz.
/// h.z ≤ 0 olan mikro-normaller tanımsızdır (D = 0).
inline float ggxD(const Vec3f& h, float ax, float ay) {
    if (h.z <= 0.0f) return 0.0f;
    ax = ggxClampAlpha(ax);
    ay = ggxClampAlpha(ay);
    float e = (h.x * h.x) / (ax * ax) + (h.y * h.y) / (ay * ay) + h.z * h.z;
    return 1.0f / (PI * ax * ay * e * e);
}

/// Smith Λ(w) fonksiyonu (GGX için kapalı form, Heitz 2014 denklem 86):
///   Λ(w) = (-1 + sqrt(1 + (ax² w.x² + ay² w.y²) / w.z²)) / 2
/// G1(w) = 1 / (1 + Λ(w)). w.z = 0 (tam teğet) için Λ → ∞, G1 → 0.
inline float ggxLambda(const Vec3f& w, float ax, float ay) {
    float z2 = w.z * w.z;
    if (z2 <= 1e-12f) return 1e12f; // /fp:fast altında inf'e güvenmiyoruz
    ax = ggxClampAlpha(ax);
    ay = ggxClampAlpha(ay);
    float t2 = (ax * ax * w.x * w.x + ay * ay * w.y * w.y) / z2;
    return 0.5f * (-1.0f + std::sqrt(1.0f + t2));
}

inline float ggxG1(const Vec3f& w, float ax, float ay) {
    return 1.0f / (1.0f + ggxLambda(w, ax, ay));
}

/// Yükseklik-korelasyonlu Smith maskeleme-gölgeleme (PBRT-v4 TrowbridgeReitz::G):
///   G(wo, wi) = 1 / (1 + Λ(wo) + Λ(wi))
/// Ayrık G1·G1 çarpımından daha doğrudur (aynı mikro-yüzey hem wo hem wi'yi kapatır).
inline float ggxG(const Vec3f& wo, const Vec3f& wi, float ax, float ay) {
    return 1.0f / (1.0f + ggxLambda(wo, ax, ay) + ggxLambda(wi, ax, ay));
}

/// wo'dan görünen normallerin yoğunluğu (Heitz 2018, denklem 3):
///   D_wo(h) = G1(wo) · |wo·h| · D(h) / |wo.z|
/// Bu, h üzerindeki katı-açı pdf'idir. Yansıyan wi'nin pdf'i için 1/(4|wo·h|)
/// Jacobian'ı ile çarpılır.
inline float ggxVisiblePdf(const Vec3f& wo, const Vec3f& h, float ax, float ay) {
    float cosO = std::abs(wo.z);
    if (cosO <= 0.0f) return 0.0f;
    return ggxG1(wo, ax, ay) * std::abs(wo.dot(h)) * ggxD(h, ax, ay) / cosO;
}

/// Görünür normal örneklemesi (Heitz 2018, "Sampling the GGX Distribution of
/// Visible Normals", JCGT 7(4)). Fikir: elipsoidi küreye germek (ax, ay ile
/// ölçekle), küre üzerinde wo'dan görünen yarım diski düzgün örneklemek, sonra
/// geri ölçeklemek. Sonuç h her zaman +z yarıküresindedir. wo alt yarıküredeyse
/// (cam içi) önce +z'ye çevrilir; PBRT-v4 de aynısını yapar.
/// Avantaj: wo'dan görünmeyen (arkası dönük) mikro-normaller hiç üretilmez,
/// eski D(h)·cos örneklemesine göre varyans belirgin düşer.
inline Vec3f ggxSampleVisibleNormal(Vec3f wo, float ax, float ay, float u1, float u2) {
    ax = ggxClampAlpha(ax);
    ay = ggxClampAlpha(ay);
    if (wo.z < 0.0f) wo = -wo;
    // 1) Yarıküre uzayına ger
    Vec3f vh = Vec3f(ax * wo.x, ay * wo.y, wo.z).normalized();
    // 2) vh etrafında ortonormal taban
    float lenSq = vh.x * vh.x + vh.y * vh.y;
    Vec3f t1 = lenSq > 0.0f ? Vec3f(-vh.y, vh.x, 0.0f) / std::sqrt(lenSq) : Vec3f(1.0f, 0.0f, 0.0f);
    Vec3f t2 = vh.cross(t1);
    // 3) Görünen yarım diski örnekle: düzgün disk, sonra alt yarıyı s oranında sıkıştır
    float r = std::sqrt(u1);
    float phi = TWO_PI * u2;
    float p1 = r * std::cos(phi);
    float p2 = r * std::sin(phi);
    float s = 0.5f * (1.0f + vh.z);
    p2 = (1.0f - s) * std::sqrt(std::max(0.0f, 1.0f - p1 * p1)) + s * p2;
    // 4) Yarıküreye yansıt
    Vec3f nh = t1 * p1 + t2 * p2 + vh * std::sqrt(std::max(0.0f, 1.0f - p1 * p1 - p2 * p2));
    // 5) Elipsoide geri dön (normaller ters-transpoze ile dönüşür: ax, ay ile çarp)
    return Vec3f(ax * nh.x, ay * nh.y, std::max(1e-6f, nh.z)).normalized();
}

/// Tek saçılmalı GGX'in yönlü albedosu E(μo, α) (F = 1): kaç enerjinin mikro-yüzeyler
/// arasında birden çok sekip "kaybolduğunu" söyler (1 − E). Pürüzlü metallerin kararmasını
/// telafi etmek için kullanılır (Turquin 2019, Kulla–Conty 2017). Ön hesaplı tablo.
float ggxDirectionalAlbedo(float cosTheta, float alpha);

/// Schlick Fresnel'li (F0) tek saçılmalı GGX lobunun, Turquin çoklu saçılma çarpanı
/// (1 + F0·(1/E − 1)) dahil yönlü albedosu E_s(μ, α, F0). Yalıtkan bir speküler katmanın
/// altındaki tabana (difüz, yüzey altı) ne kadar ışık kaldığını söyler: 1 − E_s.
/// Schlick F0'da doğrusal olduğu için iki tablodan kesin hesaplanır (split-sum gibi):
///   E_s = (F0·E_a + E_b) · (1 + F0·(1/E − 1)),  E_a = ∫(1 − (1−c)⁵)·f cos,  E_b = ∫(1−c)⁵·f cos.
float ggxSchlickAlbedo(float cosTheta, float alpha, float f0);

/// E_s'nin yarıküre ortalaması E_avg = 2∫ E_s(μ) μ dμ (Kulla–Conty normalizasyonu).
float ggxSchlickAverageAlbedo(float alpha, float f0);

/// Charlie sheen (Estevez & Kulla 2017) dağılımı: kumaş liflerinin yatay açıda parlaması.
/// D(h) = (2 + 1/α) · sin^(1/α)θh / 2π,  α = r².
inline float charlieD(float cosH, float roughness) {
    const float a = std::max(roughness * roughness, 1e-4f);
    const float inv = 1.0f / a;
    const float sin2 = std::max(0.0f, 1.0f - cosH * cosH);
    return (2.0f + inv) * std::pow(sin2, 0.5f * inv) * (0.5f * INV_PI);
}

/// Neubelt–Pettineo görünürlüğü V = 1 / (4(μi + μo − μi·μo)) (Filament, glTF örnekleri).
inline float charlieV(float cosI, float cosO) {
    return 1.0f / std::max(4.0f * (cosI + cosO - cosI * cosO), 1e-6f);
}

/// Charlie·Neubelt lobunun yönlü albedosu E_sh(μ, r) (renk = 1). Neubelt görünürlüğü fiziksel
/// bir modelden gelmediği için yatayda 1'i aşabilir; malzeme lobu bu tabloyla normalleştirir.
float charlieSheenAlbedo(float cosTheta, float roughness);

/// x⁵ — std::pow(x, 5) yerine iki çarpma + bir çarpma (Schlick için).
inline float pow5(float x) {
    float x2 = x * x;
    return x2 * x2 * x;
}

} // namespace photon
