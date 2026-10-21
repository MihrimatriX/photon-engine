// primitives.h — Sahneye eklenebilen temel şekiller (küp, küre, silindir, koni,
// düzlem, simit). Hepsi 1 birimlik kutuya sığar: x ve z'de [-0.5, 0.5], y'de
// [0, yükseklik] — tabanı y = 0'da durur, böylece zemine "konulmuş" olarak gelir.
// Üçgenler dışarıdan bakınca saat yönünün tersine (CCW) örülür: geometrik normal
// dışa bakar; cam gibi kırılan malzemeler içeri/dışarıyı buna göre ayırt eder.
#pragma once

#include "geometry/mesh.h"
#include <memory>

namespace photon {

enum class PrimitiveKind { Cube, Sphere, Cylinder, Cone, Plane, Torus };
constexpr int kPrimitiveCount = 6;

/// Kullanıcıya gösterilen ad ("Küp", "Küre", ...).
const char* primitiveName(PrimitiveKind kind);

/// Normalleri ve UV'leri olan, malzemesiz üçgen ağı.
std::shared_ptr<TriangleMesh> makePrimitiveMesh(PrimitiveKind kind);

} // namespace photon
