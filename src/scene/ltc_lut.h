// ltc_lut.h — Heitz 2016 Linearly Transformed Cosines (LTC) 64×64 GGX tabloları ve
// analitik dikdörtgen alan ışığı değerlendiricisi (Gerçek Zamanlı Viewport v2).
//
// LTC1 (64×64 RGBA32F): M⁻¹ matrisinin normalize 4 katsayısı (a, b, c, d):
//     M⁻¹ = [ a  0  b ]
//           [ 0  c  0 ]
//           [ d  0  1 ]
// LTC2 (64×64 RGBA32F): yönlü albedo ve Fresnel ayrımı (norm, fresnel, 0, 1):
//     specular ≈ (F0 * norm + (1 - F0) * fresnel) * ltcIntegrate(M⁻¹)
// Tablo koordinatları (Heitz 2016):
//     u = roughness ∈ [0, 1],  v = sqrt(1 - cosTheta) ∈ [0, 1].
#pragma once

#include "core/color/spectrum.h"
#include "core/math/vec.h"

#include <vector>

namespace photon {

struct LtcTables {
    static constexpr int kSize = 64;
    /// 64×64×4 float: (a, b, c, d) — M⁻¹ matris katsayıları (m22 = 1).
    std::vector<float> ltc1;
    /// 64×64×4 float: (magnitude, fresnel, 0, 1) — GGX yönlü albedo & Schlick terimi.
    std::vector<float> ltc2;
};

/// 64×64 Heitz 2016 GGX LTC1/LTC2 tablolarını döndürür (ilk çağrıda bir kez hesaplanır,
/// iş parçacığı güvenlidir).
const LtcTables& getLtcTables();

/// 64×64 LTC tablolarından çift doğrusal (bilinear) örnekleme.
/// @param roughness Doğrusal pürüzlülük [0, 1].
/// @param cosTheta  N·V ∈ [0, 1].
/// @param outMInv   Çıkış: (a, b, c, d) — M⁻¹ matris katsayıları.
/// @param outNorm   Çıkış: GGX yönlü albedo (F=1).
/// @param outFres   Çıkış: GGX Schlick (1 - V·H)⁵ integrali.
void sampleLtcTables(float roughness, float cosTheta, float outMInv[4], float& outNorm, float& outFres);

/// Dikdörtgen alan ışığının Heitz 2016 LTC ile analitik difüz + GGX speküler radyans katkısı.
/// @param P             Yüzey noktası (dünya uzayı).
/// @param N             Yüzey normali (birim vektör).
/// @param V             Görüş yönü P → kamera (birim vektör).
/// @param roughness     Malzeme pürüzlülüğü [0, 1].
/// @param f0            Speküler Fresnel taban yansıtıcılığı.
/// @param diffuseAlbedo Difüz albedo (metalik olmayan pay).
/// @param lightCenter   Dikdörtgen alan ışığının merkezi.
/// @param halfU         Dikdörtgenin yarı-genişlik vektörü (merkezden kenara).
/// @param halfV         Dikdörtgenin yarı-yükseklik vektörü (merkezden kenara).
/// @param radiance      Alan ışığının birim yüzey radyansı L (W/(sr·m²)).
/// @param twoSided      true ise ışık iki yüzlüdür (PhotonEngine AreaLight kuralı).
Color3f evalLtcRectArea(const Vec3f& P, const Vec3f& N, const Vec3f& V,
                        float roughness, const Color3f& f0, const Color3f& diffuseAlbedo,
                        const Vec3f& lightCenter, const Vec3f& halfU, const Vec3f& halfV,
                        const Color3f& radiance, bool twoSided = true);

} // namespace photon
