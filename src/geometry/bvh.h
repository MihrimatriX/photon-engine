#pragma once

/// @file bvh.h
/// @brief Bounding Volume Hierarchy (BVH) spatial acceleration structure in PhotonEngine.

#include "geometry/shape.h"
#include <vector>
#include <memory>
#include <cstdint>

namespace photon {

/// @brief Linear BVH node structure, optimized for cache efficiency (32 bytes).
struct BVHNode {
    AABB bounds;
    union {
        uint32_t primitivesOffset; // Leaf: index into orderedPrims vector
        uint32_t secondChildOffset; // Interior: index of the second child (left child is always current + 1)
    };
    uint16_t nPrimitives = 0;       // Leaf: number of primitives inside. Interior: 0
    uint8_t splitAxis = 0;          // Interior: 0=X, 1=Y, 2=Z

    bool isLeaf() const { return nPrimitives > 0; }
};

/// @brief Bounding Volume Hierarchy acceleration structure.
class BVH : public Shape {
public:
    BVH() = default;
    
    /// Build BVH on a list of shape primitives.
    void build(std::vector<std::shared_ptr<Shape>> primitives);

    bool intersect(Ray& ray, SurfaceInteraction& isect) const override;
    
    /// Shadow ray intersection (fast early exit, returns true on any hit)
    bool intersectAny(const Ray& ray) const;

    AABB bounds() const override {
        return m_nodes.empty() ? AABB::empty() : m_nodes[0].bounds;
    }

private:
    struct BuildPrimitive {
        size_t primIndex;
        AABB bounds;
        Vec3f centroid;

        BuildPrimitive(size_t index, const AABB& b)
            : primIndex(index), bounds(b), centroid(b.centroid()) {}
    };

    struct LinearBVHNode {
        AABB bounds;
        union {
            int primitivesOffset;  // leaf
            int secondChildOffset; // interior
        };
        uint16_t nPrimitives; // 0 -> interior node
        uint8_t axis;         // interior node: xyz
        uint8_t pad[1];       // Padding to keep structure 32-byte aligned
    };

    std::vector<std::shared_ptr<Shape>> m_primitives;
    std::vector<std::shared_ptr<Shape>> m_orderedPrims;
    std::vector<BVHNode> m_nodes;

    // Helper structures for SAH building
    struct BVHBuildNode {
        AABB bounds;
        BVHBuildNode* children[2] = {nullptr, nullptr};
        int splitAxis = 0;
        int firstPrimOffset = 0;
        int nPrimitives = 0;

        void initLeaf(int first, int n, const AABB& b) {
            firstPrimOffset = first;
            nPrimitives = n;
            bounds = b;
            children[0] = children[1] = nullptr;
        }

        void initInterior(int axis, BVHBuildNode* c0, BVHBuildNode* c1) {
            children[0] = c0;
            children[1] = c1;
            bounds = c0->bounds.merged(c1->bounds);
            splitAxis = axis;
            nPrimitives = 0;
        }
    };

    BVHBuildNode* recursiveBuild(
        std::vector<BuildPrimitive>& buildPrims,
        int start, int end, int* totalNodes,
        std::vector<std::shared_ptr<Shape>>& orderedPrims);

    int flattenBVHTree(BVHBuildNode* node, int* offset);
    void freeBuildTree(BVHBuildNode* node);
};

} // namespace photon
