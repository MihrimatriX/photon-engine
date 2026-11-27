// usd_loader.h — OpenUSD (.usd, .usda, .usdc, .usdz) yükleyicisi; Pixar OpenUSD (TOST-1.0,
// Apache-2.0 tabanlı) üzerine.
//
// Sahne, USD'nin kendi birleştirmesinden (katmanlar, referanslar, yükler, varyantlar) geçmiş
// haliyle okunur. Xform hiyerarşisi ve adlar korunur; birim (metersPerUnit) ve yukarı eksen
// (upAxis) kök dönüşümüne konur, sahne metreye ve Y-yukarıya döner. Mesh'ler (çokgen, delikli
// yüz, GeomSubset malzeme dilimleri, yüz-köşe normal ve UV), basit şekiller (küre, küp, silindir,
// koni, kapsül, düzlem), yerel örnekleme (instancing, prototipler mesh paylaşır) ve PointInstancer
// okunur. Malzemeler: UsdPreviewSurface ve MaterialX düğüm kimlikli OpenPBR / Standard Surface /
// glTF PBR (sabitler, doku düğümleri, UV dönüşümü) birleşik Principled'a çevrilir. UsdLux ışıkları
// (Rect, Disk, Sphere, Cylinder, Distant, Dome) ve UsdGeomCamera kameraları da gelir.
#pragma once

#include "io/imported_model.h"

#include <string>

namespace photon {

class UsdLoader {
public:
    /// Derlemede OpenUSD desteği var mı (PHOTON_ENABLE_USD ve kütüphane bulundu)?
    static bool available();
    /// @p unitMeters > 0: sahnenin birimi yazılmamışsa (metersPerUnit yoksa) 1 birim kaç metre.
    /// 0 = USD'nin varsayılanı (santimetre). Yazılmış birim her zaman önceliklidir.
    /// Başarısızsa boş model ve @p error dolar.
    static ImportedModel load(const std::string& path, std::string* error = nullptr, float unitMeters = 0.0f);
};

} // namespace photon
