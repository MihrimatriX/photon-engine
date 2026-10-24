// ies.h — IES LM-63 fotometrik profilleri: gerçek bir armatürün yönlere göre ışık
// şiddeti dağılımı (üreticilerin yayımladığı .ies dosyaları). Spot ışığına "gerçek lamba"
// karakteri verir: duvarda yelpaze desenleri, sert/yumuşak huzme kenarı.
//
// C tipi fotometri: dikey açı γ armatürün ekseninden (0° = nadir, aşağı; 180° = yukarı),
// yatay açı φ eksen etrafında. Dosya simetriyi son yatay açıyla bildirir: 0 → eksenel
// simetrik, 90 → dörtte bir, 180 → iki taraflı, 360 → tam.
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace photon {

struct IesProfile {
    std::vector<float> vertical;   ///< γ (derece), artan
    std::vector<float> horizontal; ///< φ (derece), artan
    std::vector<float> values;     ///< [yatay][dikey], tepe değeri 1'e normalize
    float peakCandela = 0.0f;      ///< normalize öncesi tepe (bilgi amaçlı)

    /// Normalize şiddet [0, 1]. γ ve φ derece; tablonun dışı 0.
    float evaluate(float gammaDeg, float phiDeg) const;
};

/// .ies metnini ayrıştır. Hata olursa nullptr ve @p error.
std::shared_ptr<const IesProfile> parseIes(const std::string& text, std::string* error = nullptr);
/// Dosyadan yükle (UTF-8 yol).
std::shared_ptr<const IesProfile> loadIes(const std::string& path, std::string* error = nullptr);

} // namespace photon
