#pragma once

/// @file shape.h
/// @brief Abstract base class Shape for all geometric primitives in PhotonEngine.

#include "core/math/ray.h"
#include "core/math/aabb.h"
#include "geometry/surface_interaction.h"

namespace photon {

/// @brief Abstract interface for geometric shapes.
class Shape {
public:
    virtual ~Shape() = default;

    /// Intersect ray with this shape. Returns true if hit, and populates isect.
    virtual bool intersect(Ray& ray, SurfaceInteraction& isect) const = 0;

    /// Returns the world-space bounding box of the shape.
    virtual AABB bounds() const = 0;
};

} // namespace photon
