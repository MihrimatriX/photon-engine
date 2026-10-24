// spot_light.h — Spot ışık: tek noktadan bir koni içine ışıyan delta ışık (projektör,
// ray spot, el feneri). Koni kenarı yumuşatılabilir; isteğe bağlı IES profili gerçek bir
// armatürün açısal dağılımını verir. Koni 180° ise yalnız IES dağılımı kullanılır.
#pragma once

#include "lights/ies.h"
#include "lights/light.h"
#include "core/math/frame.h"

#include <memory>

namespace photon {

class SpotLight : public Light {
public:
    /// @param direction  Koni ekseni (ışığın baktığı yön).
    /// @param intensity  Eksendeki şiddet (W/sr, renkli).
    /// @param coneAngleDeg Koninin TAM açısı (derece); ≥ 180 → koni sınırı yok.
    /// @param blend      0 = keskin kenar, 1 = merkezden kenara tamamen yumuşak.
    SpotLight(const Vec3f& position, const Vec3f& direction, const Color3f& intensity, float coneAngleDeg,
              float blend, std::shared_ptr<const IesProfile> profile = nullptr);

    LightSample sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const override;
    bool isDelta() const override { return true; }
    Color3f power() const override;
    float importance(const Vec3f& p, const Vec3f& n) const override;

    /// Işıktan @p w yönüne (birim) giden şiddetin göreli çarpanı [0, 1].
    float falloff(const Vec3f& w) const;

private:
    Vec3f m_position;
    Frame m_frame; ///< n = eksen
    Color3f m_intensity;
    float m_cosOuter = -1.0f;
    float m_cosInner = -1.0f;
    std::shared_ptr<const IesProfile> m_profile;
};

} // namespace photon
