// light_bounds.h — Sınırlı bir ışığın (ya da ışık grubunun) uzaysal ve yönsel özeti:
// kutu, toplam güç ve yayım yönlerinin konisi (Conty & Kulla 2018, PBRT-v4 §12.6.3).
//
// Işık BVH'si her düğümde bunu tutar; bir yüzey noktası için importance() "bu gruptaki ışıklar
// buraya en fazla ne kadar ışık gönderebilir"in ucuz bir tahminidir: güç / uzaklık², noktanın
// yayım konisine ne kadar dışında kaldığına ve alıcının normaline göre kısılmış. Seçim olasılığı
// buna orantılı olunca yakındaki, bize bakan, güçlü ışık grupları daha sık örneklenir.
#pragma once

#include "core/math/aabb.h"
#include "core/math/vec.h"

namespace photon {

struct LightBounds {
    AABB bounds = AABB::empty();
    Vec3f w{0.0f, 0.0f, 1.0f}; ///< yayıcı normallerinin ortalama yönü (koni ekseni)
    float phi = 0.0f;          ///< güç ölçüsü (≈ π × ışıyan şiddet; birimler gruplar arasında tutarlı)
    float cosThetaO = 1.0f;    ///< normal konisinin yarı açısının kosinüsü (1 = tek yön)
    float cosThetaE = 0.0f;    ///< her normal etrafında yayım yayılması (0 = yarıküre, Lambert)
    bool twoSided = false;     ///< iki yüzden de ışır mı (−w yönü de)

    /// @p p noktasına (yüzey normali @p n; sıfır vektörse hacim noktası) tahmini katkı. ≥ 0.
    float importance(const Vec3f& p, const Vec3f& n) const;
};

/// İki ışık grubunu kapsayan sınırlar: kutuların birleşimi, güçlerin toplamı, konilerin birleşimi.
LightBounds unionBounds(const LightBounds& a, const LightBounds& b);

} // namespace photon
