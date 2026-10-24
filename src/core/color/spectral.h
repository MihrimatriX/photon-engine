// spectral.h — Dalga boyu ↔ RGB köprüsü: dispersiyon (gökkuşağı kırılması) için.
//
// Motor RGB ile çalışır. Kırılma indisi dalga boyuna bağlı bir cama (elmas, kristal,
// prizma) çarpan yol tek bir dalga boyu λ seçer ve o andan sonra "tek renkli" ilerler:
// yol ağırlığı λ'nın RGB karşılığıyla çarpılır, cam her kesişimde n(λ) kullanır.
// Ağırlıklar, beyaz bir spektrumun (tüm λ eşit) ortalamasının tam (1, 1, 1) olacağı
// biçimde normalize edilir: dispersiyonsuz bir sahnede seçim görüntüyü değiştirmez.
//
// Renk eşleme fonksiyonları: CIE 1931 2°, Wyman–Sloan–Shirley 2013 çok-loblu analitik
// yaklaşımı. XYZ → doğrusal sRGB (D65); negatif kısımlar 0'a kırpılır (gamut dışı spektral
// renkler hafif doygunluk kaybeder, ama ağırlık asla negatif olmaz; film negatifi reddeder).
#pragma once

#include "core/color/spectrum.h"

namespace photon {

inline constexpr float kLambdaMin = 380.0f; ///< nm
inline constexpr float kLambdaMax = 730.0f; ///< nm

/// λ'nın normalize RGB ağırlığı: (1/aralık)·∫ w(λ) dλ = (1, 1, 1).
Color3f wavelengthToRGB(float lambdaNm);

/// u ∈ [0,1) ile bir dalga boyu seç (önem örnekleme: r+g+b'ye orantılı).
/// @param weight Çıkış: w(λ) / (aralık · pdf(λ)); yol ağırlığı bununla çarpılır.
///               Bileşenlerinin toplamı ~3'tür (renk değişir, toplam enerji sabit).
float sampleWavelength(float u, Color3f& weight);

/// Abbe sayısından Cauchy dağılımı: n(λ) = A + B/λ² (λ mikrometre).
/// nd: sodyum d çizgisindeki (587.6 nm) indis. V = (nd − 1)/(nF − nC); küçük V = güçlü
/// dispersiyon (flint cam ~30, crown cam ~60, elmas 55 ama yüksek nd ile çok "ateşli").
/// abbe ≤ 0 → dispersiyon yok, nd döner.
float cauchyIor(float nd, float abbe, float lambdaNm);

} // namespace photon
