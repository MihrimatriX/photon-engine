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

    /// Flip the shading normal toward the ray. Does not overwrite a geometric normal already set.
    inline void setFaceNormal(const Ray& r, const Vec3f& outwardNormal) {
        frontFace = r.direction.dot(outwardNormal) < 0.0f;
        normal = frontFace ? outwardNormal : -outwardNormal;
        if (ng.lengthSquared() == 0.0f) ng = outwardNormal;
    }
};

} // namespace photon
