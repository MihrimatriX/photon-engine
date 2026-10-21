// light.h — Tüm ışık kaynaklarının ortak arayüzü (Light) ve örnek sonucu (LightSample).
// Path tracer her yüzey noktasında bir ışık seçip sampleLi ile ona doğru bir yön örnekler
// (next-event estimation, NEE). Tahminci: f · Li · |cos θ| / pdf. Bu yüzden pdf'in KATI AÇI
// (solid angle, sr⁻¹) cinsinden olması şart; alan ışıkları kendi alan pdf'lerini çevirip verir.
#pragma once

/// @file light.h
/// @brief Abstract Light base class and LightSample structure in PhotonEngine.

#include "core/color/spectrum.h"
#include "core/math/vec.h"
#include "geometry/surface_interaction.h"

namespace photon {

/// @brief Result of sampling a light source from a shading point.
struct LightSample {
    Vec3f wi;          ///< Incident direction from shading point to light source (normalized)
    Color3f Li;        ///< Radiance arriving from the light source at the shading point
    float pdf = 0.0f;  ///< PDF with respect to solid angle
    float distance = 0.0f; ///< Distance from shading point to light source (infinity for directional lights)

    bool isValid() const { return pdf > 0.0f && !Li.isBlack(); }
};

/// @brief Abstract interface for all light sources.
class Light {
public:
    virtual ~Light() = default;

    /// @brief Sample the light source from a shading point.
    ///
    /// @param[in] si Shading point details.
    /// @param[in] sample 2D random sample in [0, 1)².
    /// @return The light sample.
    virtual LightSample sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const = 0;

    /// @brief Return true if this light is a delta distribution (point, directional).
    /// Delta lights cannot be intersected by rays directly, and require special handling in MIS.
    /// Delta ışık (nokta/yönlü) sıfır alan kaplar: rastgele bir ışının ona çarpma olasılığı 0,
    /// ancak sampleLi ile bulunabilir. pdf = 1 bir yoğunluk değil "olasılık kütlesi"dir; BSDF
    /// örneklemesi bu ışığı asla bulamayacağı için MIS uygulanmaz, NEE katkısı tam ağırlık alır.
    virtual bool isDelta() const = 0;

    /// @brief Approximate total power emitted by the light source.
    virtual Color3f power() const = 0;
};

} // namespace photon
