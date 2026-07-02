#pragma once

/// @file aabb.h
/// @brief Axis-Aligned Bounding Box (AABB) for the PhotonEngine renderer.
///
/// Used extensively in BVH traversal, spatial queries, and scene bounds
/// computation. Provides fast ray–box intersection using the slab method.

#include <cmath>
#include <algorithm>
#include <limits>

#include "core/math/vec.h"
#include "core/math/ray.h"

namespace photon {

/// @brief Axis-Aligned Bounding Box defined by two corner points.
///
/// An AABB is defined by its minimum and maximum corners (pMin, pMax).
/// An "empty" AABB has pMin > pMax in all dimensions, ensuring that
/// merging with any point or box produces the correct result.
struct AABB {
    Vec3f pMin;  ///< Minimum corner.
    Vec3f pMax;  ///< Maximum corner.

    // ── Constructors ─────────────────────────────────────────

    /// Default: constructs an empty (inverted) box.
    AABB()
        : pMin(Vec3f( std::numeric_limits<float>::max()))
        , pMax(Vec3f(-std::numeric_limits<float>::max())) {}

    /// Construct an AABB containing a single point.
    explicit constexpr AABB(const Vec3f& p) : pMin(p), pMax(p) {}

    /// Construct an AABB from two corner points.
    constexpr AABB(const Vec3f& a, const Vec3f& b)
        : pMin(a.cwiseMin(b)), pMax(a.cwiseMax(b)) {}

    // ── Ray intersection ─────────────────────────────────────

    /// Fast ray–AABB intersection using the slab method (Williams et al. 2005).
    ///
    /// @param[in]  ray   The ray to test against.
    /// @param[out] tNear Parametric distance to the near intersection.
    /// @param[out] tFar  Parametric distance to the far intersection.
    /// @return @c true if the ray intersects the box within [ray.tMin, ray.tMax].
    inline bool intersect(const Ray& ray, float& tNear, float& tFar) const {
        float t0 = ray.tMin;
        float t1 = ray.tMax;

        for (int i = 0; i < 3; ++i) {
            float invDir = 1.0f / ray.direction[i];
            float tSlabNear = (pMin[i] - ray.origin[i]) * invDir;
            float tSlabFar  = (pMax[i] - ray.origin[i]) * invDir;

            // Swap if direction is negative
            if (invDir < 0.0f) std::swap(tSlabNear, tSlabFar);

            t0 = std::max(t0, tSlabNear);
            t1 = std::min(t1, tSlabFar);

            if (t0 > t1) return false;
        }

        tNear = t0;
        tFar  = t1;
        return true;
    }

    // ── Merging ──────────────────────────────────────────────

    /// Return a new AABB that encloses both this box and @p other.
    constexpr AABB merged(const AABB& other) const {
        return AABB(pMin.cwiseMin(other.pMin), pMax.cwiseMax(other.pMax));
    }

    /// Expand this AABB in-place to include @p other.
    constexpr void merge(const AABB& other) {
        pMin = pMin.cwiseMin(other.pMin);
        pMax = pMax.cwiseMax(other.pMax);
    }

    /// Expand this AABB in-place to include point @p p.
    constexpr void merge(const Vec3f& p) {
        pMin = pMin.cwiseMin(p);
        pMax = pMax.cwiseMax(p);
    }

    // ── Geometric queries ────────────────────────────────────

    /// Center point of the AABB.
    constexpr Vec3f centroid() const {
        return (pMin + pMax) * 0.5f;
    }

    /// Diagonal vector from pMin to pMax.
    constexpr Vec3f diagonal() const {
        return pMax - pMin;
    }

    /// Surface area of the AABB (sum of all 6 face areas).
    /// Used by SAH-based BVH builders.
    constexpr float surfaceArea() const {
        Vec3f d = diagonal();
        return 2.0f * (d.x * d.y + d.x * d.z + d.y * d.z);
    }

    /// Index of the axis with the largest extent (0=x, 1=y, 2=z).
    constexpr int maxExtent() const {
        Vec3f d = diagonal();
        if (d.x > d.y && d.x > d.z) return 0;
        if (d.y > d.z)              return 1;
        return 2;
    }

    /// Test whether point @p p is inside the AABB (inclusive bounds).
    constexpr bool contains(const Vec3f& p) const {
        return p.x >= pMin.x && p.x <= pMax.x
            && p.y >= pMin.y && p.y <= pMax.y
            && p.z >= pMin.z && p.z <= pMax.z;
    }

    /// Return the relative position of point @p p within the AABB,
    /// where pMin maps to (0,0,0) and pMax maps to (1,1,1).
    /// Undefined for an empty AABB.
    inline Vec3f offset(const Vec3f& p) const {
        Vec3f o = p - pMin;
        Vec3f d = diagonal();
        if (d.x > 0.0f) o.x /= d.x;
        if (d.y > 0.0f) o.y /= d.y;
        if (d.z > 0.0f) o.z /= d.z;
        return o;
    }

    // ── Static factory ───────────────────────────────────────

    /// Return an empty (inverted) AABB — merging with anything yields that thing's bounds.
    static inline AABB empty() {
        return AABB();  // Default constructor produces an empty box
    }
};

} // namespace photon
