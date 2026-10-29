// ray_spawn.h — Yüzeyden çıkan ışınların sağlam (ölçekten bağımsız) başlatılması.
//
// Kesişim noktası kayan noktayla hesaplandığı için yüzeyin ya biraz önünde ya biraz
// arkasındadır. Arkasındaysa aynı yüzeyden çıkan ışın kendine çarpar ("gölge sivilcesi",
// acne). Klasik çare sabit bir ofsettir (1e-4), ama sabit değer ölçeğe bağlıdır: milimetreyle
// gelen bir araba modelinde (koordinatlar ~4500) 1e-4 bir float adımından (ULP ≈ 5e-4) küçüktür
// → sivilce; metreyle gelen bir yüzükte (0.02) ise ince ayrıntıyı atlar → ışık sızması.
//
// PBRT-v4 yöntemi (§6.8.6): şekil, noktayla birlikte bileşen başına mutlak hata sınırını
// (pError) hesaplar. Işın, geometrik normal boyunca d = |n|·pError kadar itilir (hata kutusunun
// normal yönündeki izdüşümü: nokta kesinlikle yüzeyin doğru tarafında kalır) ve sonuç bir ULP
// daha dışa yuvarlanır. Ofset noktanın büyüklüğüyle orantılıdır; ışın tMin = 0 ile atılır.
#pragma once

#include "core/math/float_bits.h"
#include "core/math/ray.h"
#include "geometry/surface_interaction.h"

#include <algorithm>
#include <cmath>

namespace photon {

/// @p p'yi (hata sınırı @p pError) @p n normali boyunca, @p w'nin gittiği tarafa iter.
inline Vec3f offsetRayOrigin(const Vec3f& p, const Vec3f& pError, const Vec3f& n, const Vec3f& w) {
    const float d = n.cwiseAbs().dot(pError);
    Vec3f offset = n * d;
    if (w.dot(n) < 0.0f) offset = -offset;
    Vec3f po = p + offset;
    for (int i = 0; i < 3; ++i) {
        if (offset[i] > 0.0f) po[i] = nextFloatUp(po[i]);
        else if (offset[i] < 0.0f) po[i] = nextFloatDown(po[i]);
    }
    return po;
}

/// Yüzeyden @p dir yönünde çıkan ışının başlangıcı. Ofset GEOMETRİK normal boyuncadır:
/// gölgelendirme normali (yumuşatılmış, normal haritalı) yüzeyin gerçek yönünü bilmez.
inline Vec3f spawnOrigin(const SurfaceInteraction& si, const Vec3f& dir) {
    const Vec3f n = si.ng.lengthSquared() > 0.0f ? si.ng : si.normal;
    return offsetRayOrigin(si.point, si.pError, n, dir);
}

/// Gölge ışınının başlangıcı, gölge terminatörü düzeltmesiyle (Hanika 2021; Cycles
/// "shadow terminator geometry offset" ile aynı karışım). Kaba üçgenli yumuşak modelde ışığa
/// neredeyse teğet bakan noktalarda (N·L ya da Ng·L < kesim) başlangıç, köşe normallerinin
/// tarif ettiği yumuşak yüzeydeki noktaya (pSmooth) doğru kaydırılır; böylece yumuşak normalin
/// "aydınlık" dediği yer kaba üçgenin kendisi tarafından gölgelenmez. Yalnız gölge ışınlarında:
/// devam eden yollarda (cam, iç yansıma) ışının cismin yanlış tarafından başlamasını önler.
inline Vec3f shadowOrigin(const SurfaceInteraction& si, const Vec3f& toLight) {
    const Vec3f n = si.ng.lengthSquared() > 0.0f ? si.ng : si.normal;
    Vec3f p = si.point;
    if (si.hasSmoothPoint) {
        const Vec3f shadingOut = si.frontFace ? si.normal : -si.normal;
        const Vec3f ngOut = n.dot(shadingOut) >= 0.0f ? n : -n;
        const float NL = shadingOut.dot(toLight);
        if (NL > 0.0f) { // ışık yumuşak yüzeyin dış tarafında (geçirgen gölge ışınına uygulanmaz)
            constexpr float kCutoff = 0.1f;
            const float NgL = ngOut.dot(toLight);
            const float amount = NL < kCutoff ? std::clamp(2.0f - (NgL + NL) / kCutoff, 0.0f, 1.0f)
                                              : std::clamp(1.0f - NgL / kCutoff, 0.0f, 1.0f);
            if (amount > 0.0f) p += (si.pSmooth - si.point) * amount;
        }
    }
    return offsetRayOrigin(p, si.pError, n, toLight);
}

/// Yüzeyden @p dir yönünde sonsuza giden ışın (tMin = 0: ofset zaten yapıldı).
inline Ray spawnRay(const SurfaceInteraction& si, const Vec3f& dir) {
    return Ray(spawnOrigin(si, dir), dir, 0.0f, 1e30f);
}

/// Yüzeyden @p target noktasına giden gölge ışını; hedefin hemen önünde durur (hedef bir ışığın
/// yüzeyindeyse o yüzey kendi örneğini örtmesin). Pay göreli: mesafenin 1e-4'ü ve hedefin kendi
/// kayan nokta hatası (~16 ULP). Kurulamazsa (hedef başlangıca fazla yakın) false.
inline bool spawnRayTo(const SurfaceInteraction& si, const Vec3f& target, Ray& out) {
    const Vec3f toTarget = target - si.point;
    const float len = toTarget.length();
    if (!(len > 0.0f)) return false;
    const Vec3f origin = shadowOrigin(si, toTarget / len);
    const Vec3f d = target - origin;
    const float dist = d.length();
    const float slack = std::max(dist * 1e-4f, 2e-6f * target.cwiseAbs().maxComponent());
    if (!(dist > slack)) return false;
    out = Ray(origin, d / dist, 0.0f, dist - slack);
    return true;
}

} // namespace photon
