// surface_interaction.h — Işın–yüzey kesişiminin kaydı (nokta, normaller, UV, malzeme).
// Şekiller bunu doldurur; malzemeler ve integrator ışığın orada nasıl saçılacağını hesaplar.
#pragma once

/// @file surface_interaction.h
/// @brief SurfaceInteraction structure storing geometric details of an intersection point.

#include "core/math/vec.h"
#include "core/math/ray.h"

namespace photon {

class Material; // Forward declaration

/// @brief Geometric information at a ray-surface intersection point.
struct SurfaceInteraction {
    Vec3f point;            ///< Position of intersection in world coordinates
    Vec3f normal;           ///< Shading normal, flipped to face the incident ray
    Vec3f ng;               ///< Unflipped geometric normal (outward). Dielectric enter/exit uses this.
    Vec3f tangent;          ///< Shading tangent for anisotropic materials and normal mapping
    Vec2f uv;               ///< UV texture coordinates
    float t = -1.0f;        ///< Distance along the ray of intersection
    const Material* material = nullptr; ///< Material of the intersected surface
    const void* hitObject = nullptr;    ///< Çarpılan şekil/mesh (seçim için; BVH yazar)
    bool frontFace = true;  ///< True if ray hit front side of the surface
    /// Yolun dalga boyu (nm). 0 = RGB modu. Dispersiyonlu camda integratör seçer ve
    /// malzemeye bu alanla iletir (kırılma indisi dalga boyuna göre değişir).
    float wavelength = 0.0f;
    /// 'point'in bileşen başına mutlak hata sınırı (PBRT-v4 pError). Yüzeyden çıkan ışın bu
    /// kadar itilir: ofset sahne ölçeğiyle büyür (mm'lik CAD'de de, metrelik mücevherde de doğru).
    Vec3f pError;
    /// Gölge terminatörü noktası (Hanika 2021): kaba üçgenli yumuşak modelde, noktanın köşe
    /// normallerinin teğet düzlemlerine izdüşümü. Terminatör yakınındaki gölge ışınları buradan
    /// başlar (ray_spawn.h shadowOrigin); yumuşak normalin "aydınlık" dediği yer kaba üçgen
    /// tarafından gölgelenmez.
    Vec3f pSmooth;
    bool hasSmoothPoint = false;
    /// Kesişilen primitifin sırası (mesh içindeki üçgen); yoksa −1. Yayıcı mesh pdf'i için.
    int primIndex = -1;

    /// İki normal tutulur: 'normal' daima gelen ışına bakar (malzemeler tek yarıküreyle çalışsın),
    /// 'ng' ise çevrilmemiş dış normaldir. frontFace ışının dışarıdan mı içeriden mi geldiğini
    /// söyler; cam, kırılma oranını (η veya 1/η) buna göre seçer.
    /// Flip the shading normal toward the ray. Does not overwrite a geometric normal already set.
    inline void setFaceNormal(const Ray& r, const Vec3f& outwardNormal) {
        frontFace = r.direction.dot(outwardNormal) < 0.0f;
        normal = frontFace ? outwardNormal : -outwardNormal;
        if (ng.lengthSquared() == 0.0f) ng = outwardNormal;
    }
};

} // namespace photon
