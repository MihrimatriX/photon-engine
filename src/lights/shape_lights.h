// shape_lights.h — Tam şekilli alan ışıkları: disk (elips), küre ve silindir (tüp). Hepsi düzgün
// Lambert yayıcıdır (radyans L her yönde aynı) ve sahnede kendi analitik şekilleriyle (Disk,
// Sphere, Cylinder) kesişilir; NEE ile BSDF isabeti aynı yüzeyi görür, MIS tutarlıdır.
//
//   Disk:     tek ya da iki yüzlü; alanda düzgün örnekleme.
//   Küre:     dışa ışır; alıcıdan görülen koni içinde katı açıda düzgün örnekleme (PBRT-v4).
//   Silindir: yan yüzey dışa ışır, uçlar ışımaz (USD CylinderLight); alıcının gördüğü yarım
//             tüpte (görünür açı aralığı) alanda düzgün örnekleme.
#pragma once

#include "lights/light.h"

namespace photon {

/// Merkez + iki dik yarı eksen vektörü (uzunlukları yarıçaplar). Normal ex × ey yönündedir;
/// tek yüzlüde ışık yalnız o tarafa çıkar.
class DiskLight : public Light {
public:
    DiskLight(const Vec3f& center, const Vec3f& ex, const Vec3f& ey, const Color3f& radiance, bool twoSided);

    LightSample sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const override;
    bool isDelta() const override { return false; }
    Color3f power() const override;
    float importance(const Vec3f& p, const Vec3f& n) const override;
    float pdfLiHit(const Vec3f& ref, const SurfaceInteraction& hit) const override;
    bool bounds(LightBounds& out) const override;

    void setVisibleToCamera(bool v) { m_visibleToCamera = v; }
    bool visibleToCamera() const { return m_visibleToCamera; }

    const Vec3f& center() const { return m_center; }
    const Vec3f& ex() const { return m_ex; }
    const Vec3f& ey() const { return m_ey; }
    const Vec3f& normal() const { return m_normal; }
    float area() const { return m_area; }
    const Color3f& radiance() const { return m_radiance; }
    bool twoSided() const { return m_twoSided; }

private:
    bool facing(const Vec3f& ref) const { return m_twoSided || (ref - m_center).dot(m_normal) > 0.0f; }

    Vec3f m_center, m_ex, m_ey, m_normal;
    Color3f m_radiance;
    float m_area = 0.0f;
    bool m_twoSided = false;
    bool m_visibleToCamera = true;
};

/// Yarıçaplı nokta ışık: yüzeyi dışa doğru düzgün L radyansıyla ışıyan küre. Uzaktan görülen
/// şiddet I = L · π r² olduğundan aynı I'yı veren nokta ışığın yumuşak gölgeli karşılığıdır.
class SphereLight : public Light {
public:
    SphereLight(const Vec3f& center, float radius, const Color3f& radiance);

    LightSample sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const override;
    bool isDelta() const override { return false; }
    Color3f power() const override;
    float importance(const Vec3f& p, const Vec3f& n) const override;
    float pdfLiHit(const Vec3f& ref, const SurfaceInteraction& hit) const override;
    bool bounds(LightBounds& out) const override;

    void setVisibleToCamera(bool v) { m_visibleToCamera = v; }
    bool visibleToCamera() const { return m_visibleToCamera; }

    const Vec3f& center() const { return m_center; }
    float radius() const { return m_radius; }
    const Color3f& radiance() const { return m_radiance; }

    /// @p ref'ten görülen koninin katı açısı (sr); ref kürenin içindeyse 0.
    float solidAngle(const Vec3f& ref) const;

private:
    Vec3f m_center;
    float m_radius = 0.0f;
    Color3f m_radiance;
    bool m_visibleToCamera = true;
};

/// Açık tüp: ekseni center'dan geçen, yarı boy vektörü halfAxis, yarıçapı radius. Yan yüzey
/// dışa ışır; uçlar ışımaz.
class CylinderLight : public Light {
public:
    CylinderLight(const Vec3f& center, const Vec3f& halfAxis, float radius, const Color3f& radiance);

    LightSample sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const override;
    bool isDelta() const override { return false; }
    Color3f power() const override;
    float importance(const Vec3f& p, const Vec3f& n) const override;
    float pdfLiHit(const Vec3f& ref, const SurfaceInteraction& hit) const override;
    bool bounds(LightBounds& out) const override;

    void setVisibleToCamera(bool v) { m_visibleToCamera = v; }
    bool visibleToCamera() const { return m_visibleToCamera; }

    const Vec3f& center() const { return m_center; }
    const Vec3f& axis() const { return m_axis; } ///< birim eksen
    float halfLength() const { return m_halfLength; }
    float radius() const { return m_radius; }
    const Color3f& radiance() const { return m_radiance; }
    float area() const; ///< yan yüzey alanı 2π r ℓ

private:
    /// @p ref'ten görülen yan yüzey: eksen etrafında φ0 ± α açı aralığı. false: hiç görünmez
    /// (ref tüpün sonsuz uzantısının içinde).
    bool visibleArc(const Vec3f& ref, float& phi0, float& alpha) const;

    Vec3f m_center, m_axis, m_b, m_c; ///< m_b, m_c: eksene dik birim taban
    float m_halfLength = 0.0f;
    float m_radius = 0.0f;
    Color3f m_radiance;
    bool m_visibleToCamera = true;
};

} // namespace photon
