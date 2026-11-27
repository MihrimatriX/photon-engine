// cylinder.h — Analitik açık silindir (kapaksız tüp) şekli. Silindir (tüp) ışığının yan
// yüzeyidir; USD CylinderLight gibi uçlardan ışımaz ve uç kapakları yoktur.
#pragma once

#include "geometry/shape.h"

namespace photon {

class Material;

/// Ekseni @p center'dan geçen, yarı boyu vektörü @p halfAxis (eksen yönü × uzunluk/2) ve
/// yarıçapı @p radius olan açık silindir. Normal eksenden dışarı bakar.
class Cylinder : public Shape {
public:
    Cylinder(const Vec3f& center, const Vec3f& halfAxis, float radius, const Material* material);

    bool intersect(Ray& ray, SurfaceInteraction& isect) const override;
    AABB bounds() const override;

    const Vec3f& center() const { return m_center; }
    const Vec3f& axis() const { return m_axis; }   ///< birim eksen
    float halfLength() const { return m_halfLength; }
    float radius() const { return m_radius; }
    const Material* material() const { return m_material; }

private:
    Vec3f m_center, m_axis;
    Vec3f m_b, m_c; ///< eksene dik birim taban (uv açısı için)
    float m_halfLength = 0.0f;
    float m_radius = 0.0f;
    const Material* m_material;
};

} // namespace photon
