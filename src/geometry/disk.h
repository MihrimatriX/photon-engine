// disk.h — Analitik düz disk (elips) şekli: merkez + iki yarı eksen vektörü. Disk ışığının
// yüzeyidir; üçgenleştirilmez, böylece ışın isabeti ile ışık örneklemesi aynı alanı görür.
#pragma once

#include "geometry/shape.h"

namespace photon {

class Material;

/// Merkezi @p center, yarı eksenleri @p ex ve @p ey (birbirine dik, uzunlukları yarıçaplar) olan
/// düz elips. Dışa dönük normal ex × ey yönündedir. ex ve ey eşit uzunluktaysa dairedir.
class Disk : public Shape {
public:
    Disk(const Vec3f& center, const Vec3f& ex, const Vec3f& ey, const Material* material);

    bool intersect(Ray& ray, SurfaceInteraction& isect) const override;
    AABB bounds() const override;

    const Vec3f& center() const { return m_center; }
    const Vec3f& ex() const { return m_ex; }
    const Vec3f& ey() const { return m_ey; }
    const Vec3f& normal() const { return m_normal; }
    const Material* material() const { return m_material; }

private:
    Vec3f m_center, m_ex, m_ey, m_normal;
    Vec3f m_exInv, m_eyInv; ///< ex / |ex|², ey / |ey|²: yerel (s, t) koordinatları için
    const Material* m_material;
};

} // namespace photon
