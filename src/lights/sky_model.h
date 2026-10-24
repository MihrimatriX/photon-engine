// sky_model.h — Fiziksel gökyüzü (Nishita 1993, tek saçılma) ve güneş konumu.
//
// Atmosfer küresel kabuktur: Rayleigh (hava molekülleri, mavi gök), Mie (aerosol/pus,
// güneş çevresindeki parlak hale) ve ozon (alacakaranlığın mavisi; yalnız soğurur).
// Her bakış yönü için ışın atmosfer boyunca yürünür; her adımda güneşten gelen ışığın
// oraya kadar ve oradan göze kadar zayıflaması (Beer–Lambert) ile saçılma faz
// fonksiyonları çarpılıp toplanır. Sonuç equirect bir HDR görüntüdür; güneş diski ayrı
// döner (EnvironmentLight onu koni olarak örnekler: keskin, gürültüsüz gölgeler).
// Kaynaklar: Nishita vd. 1993; Scratchapixel "Simulating the Colors of the Sky";
// Bruneton & Neyret 2008 (ozon profili); Blender Cycles Nishita gök dokusu.
#pragma once

#include "core/color/spectrum.h"
#include "core/image/image.h"
#include "core/math/vec.h"

#include <memory>

namespace photon {

struct SkyParams {
    float sunElevationDeg = 35.0f; ///< ufkun üstü (negatif = güneş battı)
    float sunAzimuthDeg = 45.0f;   ///< sahne kuralı: +X'ten +Z'ye (yönlü ışıkla aynı)
    float turbidity = 2.5f;        ///< pus: 1 = çok berrak, 10 = çok puslu
    float ozone = 1.0f;            ///< ozon çarpanı (0 = yok)
    float groundAlbedo = 0.3f;     ///< ufkun altı (yer) yansıtması
    float sunSizeDeg = 0.545f;     ///< güneş diskinin açısal çapı (gerçek ~0.53°)
    float sunStrength = 3.0f;      ///< atmosfer üstünde güneş ışınımı (sahne birimi)
    int width = 512;               ///< equirect genişliği (yükseklik yarısı)
};

struct SkyResult {
    std::shared_ptr<Image> image; ///< güneş diski hariç gök (equirect, döndürme 0)
    Vec3f sunDirection;           ///< güneşe doğru birim yön
    Color3f sunRadiance;          ///< disk radyansı (atmosferden geçtikten sonra)
    float sunCosMax = 1.0f;       ///< disk yarı açısının kosinüsü
};

SkyResult buildPhysicalSky(const SkyParams& params);

/// NOAA basitleştirilmiş güneş konumu. @p dayOfYear 1..366, @p localHours yerel saat
/// (ondalıklı), @p utcOffsetHours saat dilimi (Türkiye +3). Çıktı: yükseklik (derece) ve
/// pusula azimutu (derece, kuzeyden doğuya). Hata ~0.1°, gölge çalışması için yeterli.
void solarPosition(float latitudeDeg, float longitudeDeg, int dayOfYear, float localHours, float utcOffsetHours,
                   float& elevationDeg, float& compassAzimuthDeg);

/// Pusula azimutunu sahne azimutuna çevir. Sahnede kuzey −Z, doğu +X kabul edilir;
/// @p northOffsetDeg sahnenin kuzeyini döndürür.
inline float compassToSceneAzimuth(float compassDeg, float northOffsetDeg = 0.0f) {
    return compassDeg - 90.0f + northOffsetDeg;
}

} // namespace photon
