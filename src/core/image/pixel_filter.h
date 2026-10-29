// pixel_filter.h — Piksel yeniden yapılandırma filtresi, filtre önem örneklemesiyle (FIS).
//
// Bir piksel, sensörde sonlu bir alanı temsil eder; görüntü değeri = sahnenin bir filtre
// çekirdeğiyle ağırlıklı ortalamasıdır. Kutu filtre (her örnek yalnız kendi pikseline)
// keskin ama merdivenli kenar ve titreşen ince ayrıntı verir. Daha geniş, yumuşak çekirdekler
// (Blackman–Harris, Gauss) bu örtüşmeyi (aliasing) bastırır.
//
// Uygulama: örnekler pikselin içine düzgün değil, FİLTRENİN KENDİ DAĞILIMIYLA (filtre ∝ pdf)
// yerleştirilir ve ağırlık sabit 1 olur (PBRT-v4 FilterSampler, Cycles). Komşu piksellere
// "sıçratma" (splat) gerekmez; her piksel kendi örneklerini toplar, filtre pozitif olduğu için
// ağırlık işareti değişmez ve OIDN'in AOV kuralına (aynı alt-piksel konumu) uyulur.
// Filtre ayrılabilir: x ve y ofsetleri aynı 1B tablodan bağımsız çekilir; Sobol'ün ilk iki
// boyutunun tabakalanması ters CDF dönüşümüyle korunur.
#pragma once

#include "core/math/vec.h"

#include <vector>

namespace photon {

enum class PixelFilterType {
    Box = 0,            ///< genişlik 1 = klasik piksel içi titreme (en keskin, merdivenli)
    Triangle = 1,       ///< çadır
    Gaussian = 2,       ///< σ = genişlik/6, kesilmiş
    BlackmanHarris = 3, ///< Cycles varsayılanı: Gauss'a yakın, daha az bulanık
};

struct PixelFilter {
    PixelFilterType type = PixelFilterType::BlackmanHarris;
    float width = 1.5f; ///< piksel cinsinden toplam genişlik (yarıçap = width/2)

    bool operator==(const PixelFilter&) const = default;
};

/// Filtre adı ("box", "triangle", "gaussian", "blackman-harris") ↔ tür. Bilinmeyen ad → false.
bool parsePixelFilter(const char* name, PixelFilterType& out);
const char* pixelFilterName(PixelFilterType type);

/// 1B filtre dağılımının ters CDF'i. Kurulumu ucuzdur (512 hücre); render başına bir kez.
class PixelFilterSampler {
public:
    explicit PixelFilterSampler(const PixelFilter& filter = {});

    /// u ∈ [0,1)² → piksel merkezinden ofset (piksel). Kutu filtrede (u − 0.5)·genişlik.
    Vec2f sample(const Vec2f& u) const { return Vec2f(sample1D(u.x), sample1D(u.y)); }
    float sample1D(float u) const;

    /// 1B filtre değeri (normalize değil); testler için.
    float evaluate(float x) const;
    float radius() const { return m_radius; }

private:
    PixelFilter m_filter;
    float m_radius = 0.5f;
    std::vector<float> m_cdf; ///< m_cdf[i] = ∫ f, [−r, −r + i·Δ]; son eleman 1
};

} // namespace photon
