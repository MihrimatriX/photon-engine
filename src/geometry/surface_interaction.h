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
    Vec3f normal;           ///< Geometric/shading normal at intersection (always normalized)
    Vec3f tangent;          ///< Shading tangent for anisotropic materials and normal mapping
    Vec2f uv;               ///< UV texture coordinates
    float t = -1.0f;        ///< Distance along the ray of intersection
    const Material* material = nullptr; ///< Material of the intersected surface
    bool frontFace = true;  ///< True if ray hit front side of the surface

    /// Determine normal direction relative to the ray.
    /// If the ray points inside the surface, normal is flipped.
    inline void setFaceNormal(const Ray& r, const Vec3f& outwardNormal) {
        frontFace = r.direction.dot(outwardNormal) < 0.0f;
        normal = frontFace ? outwardNormal : -outwardNormal;
    }
};

} // namespace photon
