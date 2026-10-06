// embree_accel.h — Intel Embree 4 ile ışın-sahne kesişimi (üretim hızlandırıcısı).
//
// Embree, SIMD (AVX2) ile aynı anda birçok kutu/üçgen test eden, çok iyi optimize
// edilmiş bir BVH kütüphanesidir. Motorun kendi BVH'si (geometry/bvh.*) öğrenme ve
// test referansı olarak kalır; Embree derlemede bulunursa Scene onu kullanır.
//
// Embree yalnız "ışın neye, hangi t'de, hangi barisentrik (u,v) ile çarptı" sorusunu
// cevaplar. Normal, UV ve teğet gibi gölgelendirme verisi motorun kendi kodunda
// (Triangle::fillHit) hesaplanır; böylece iki hızlandırıcı aynı görüntüyü üretir.
#pragma once

#include "core/math/aabb.h"
#include "core/math/ray.h"
#include "geometry/surface_interaction.h"

#include <memory>
#include <vector>

namespace photon {

class Shape;

class EmbreeAccel {
public:
    EmbreeAccel();
    ~EmbreeAccel();
    EmbreeAccel(const EmbreeAccel&) = delete;
    EmbreeAccel& operator=(const EmbreeAccel&) = delete;

    /// Bu derleme Embree ile bağlandıysa true.
    static bool available();

    void build(const std::vector<std::shared_ptr<Shape>>& shapes);
    bool intersect(Ray& ray, SurfaceInteraction& isect) const;
    bool intersectAny(const Ray& ray) const;
    AABB bounds() const { return m_bounds; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    AABB m_bounds = AABB::empty();
};

} // namespace photon
