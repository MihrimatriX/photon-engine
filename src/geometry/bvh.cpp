#include "geometry/bvh.h"
#include "geometry/mesh.h"
#include <algorithm>
#include <iostream>
#include <vector>

namespace photon {

void BVH::build(std::vector<std::shared_ptr<Shape>> primitives) {
    m_keep = std::move(primitives);
    m_nodes.clear();
    m_prims.clear();

    std::vector<PrimRef> refs;
    for (auto& shape : m_keep) {
        if (auto* mesh = dynamic_cast<TriangleMesh*>(shape.get())) {
            uint32_t n = static_cast<uint32_t>(mesh->numTriangles());
            for (uint32_t i = 0; i < n; ++i) refs.push_back(PrimRef{nullptr, mesh, i});
        } else if (shape) {
            refs.push_back(PrimRef{shape.get(), nullptr, 0});
        }
    }
    if (refs.empty()) return;

    std::vector<BuildPrimitive> buildPrims;
    buildPrims.reserve(refs.size());
    for (size_t i = 0; i < refs.size(); ++i) {
        AABB bounds = refs[i].mesh ? refs[i].mesh->triangleBounds(refs[i].tri) : refs[i].shape->bounds();
        buildPrims.emplace_back(i, bounds);
    }

    int totalNodes = 0;
    std::vector<PrimRef> orderedPrims;
    orderedPrims.reserve(refs.size());

    BVHBuildNode* root = recursiveBuild(buildPrims, 0, static_cast<int>(buildPrims.size()),
                                        &totalNodes, orderedPrims, refs);
    m_prims = std::move(orderedPrims);

    m_nodes.resize(totalNodes);
    int offset = 0;
    flattenBVHTree(root, &offset);
    freeBuildTree(root);
}

struct BucketInfo {
    int count = 0;
    AABB bounds = AABB::empty();
};

BVH::BVHBuildNode* BVH::recursiveBuild(
    std::vector<BuildPrimitive>& buildPrims,
    int start, int end, int* totalNodes,
    std::vector<PrimRef>& orderedPrims,
    const std::vector<PrimRef>& refs) {
    
    (*totalNodes)++;
    BVHBuildNode* node = new BVHBuildNode();

    // Compute bounds for all primitives in this node
    AABB bounds = AABB::empty();
    for (int i = start; i < end; ++i) {
        bounds = bounds.merged(buildPrims[i].bounds);
    }

    int nPrims = end - start;
    if (nPrims == 1) {
        // Create leaf node
        int firstPrimOffset = static_cast<int>(orderedPrims.size());
        for (int i = start; i < end; ++i) {
            orderedPrims.push_back(refs[buildPrims[i].primIndex]);
        }
        node->initLeaf(firstPrimOffset, nPrims, bounds);
        return node;
    }

    // Compute centroid bounds
    AABB centroidBounds = AABB::empty();
    for (int i = start; i < end; ++i) {
        centroidBounds.merge(buildPrims[i].centroid);
    }

    int dim = centroidBounds.maxExtent();

    // Degenerate case: all primitives have same centroid. Midpoint so a flat
    // mesh cannot become one uint16 leaf.
    if (centroidBounds.pMax[dim] == centroidBounds.pMin[dim]) {
        if (nPrims > 4) {
            int mid = (start + end) / 2;
            node->initInterior(dim,
                               recursiveBuild(buildPrims, start, mid, totalNodes, orderedPrims, refs),
                               recursiveBuild(buildPrims, mid, end, totalNodes, orderedPrims, refs));
            return node;
        }
        int firstPrimOffset = static_cast<int>(orderedPrims.size());
        for (int i = start; i < end; ++i) {
            orderedPrims.push_back(refs[buildPrims[i].primIndex]);
        }
        node->initLeaf(firstPrimOffset, nPrims, bounds);
        return node;
    }

    // SAH Split heuristic
    int mid = (start + end) / 2;
    if (nPrims <= 2) {
        // Just split equally
        std::nth_element(buildPrims.begin() + start, buildPrims.begin() + mid, buildPrims.begin() + end,
                         [dim](const BuildPrimitive& a, const BuildPrimitive& b) {
                             return a.centroid[dim] < b.centroid[dim];
                         });
    } else {
        // Binned SAH building (12 bins)
        constexpr int nBuckets = 12;
        BucketInfo buckets[nBuckets];

        for (int i = start; i < end; ++i) {
            int b = static_cast<int>(nBuckets * centroidBounds.offset(buildPrims[i].centroid)[dim]);
            if (b == nBuckets) b = nBuckets - 1;
            buckets[b].count++;
            buckets[b].bounds = buckets[b].bounds.merged(buildPrims[i].bounds);
        }

        // Compute cost for splitting at each bucket boundary
        float cost[nBuckets - 1];
        for (int i = 0; i < nBuckets - 1; ++i) {
            AABB b0 = AABB::empty();
            AABB b1 = AABB::empty();
            int count0 = 0;
            int count1 = 0;
            for (int j = 0; j <= i; ++j) {
                b0 = b0.merged(buckets[j].bounds);
                count0 += buckets[j].count;
            }
            for (int j = i + 1; j < nBuckets; ++j) {
                b1 = b1.merged(buckets[j].bounds);
                count1 += buckets[j].count;
            }
            cost[i] = 1.0f + (count0 * b0.surfaceArea() + count1 * b1.surfaceArea()) / bounds.surfaceArea();
        }

        // Find minimum cost split
        float minCost = cost[0];
        int minCostSplitBucket = 0;
        for (int i = 1; i < nBuckets - 1; ++i) {
            if (cost[i] < minCost) {
                minCost = cost[i];
                minCostSplitBucket = i;
            }
        }

        // Split cost vs leaf cost
        float leafCost = static_cast<float>(nPrims);
        if (nPrims > 255 || minCost < leafCost) {
            // Partition primitives
            auto pmid = std::partition(buildPrims.begin() + start, buildPrims.begin() + end,
                                       [=](const BuildPrimitive& bp) {
                                           int b = static_cast<int>(nBuckets * centroidBounds.offset(bp.centroid)[dim]);
                                           if (b == nBuckets) b = nBuckets - 1;
                                           return b <= minCostSplitBucket;
                                       });
            mid = static_cast<int>(pmid - buildPrims.begin());
            if (mid != start && mid != end) {
                // Successful split
                node->initInterior(dim,
                                   recursiveBuild(buildPrims, start, mid, totalNodes, orderedPrims, refs),
                                   recursiveBuild(buildPrims, mid, end, totalNodes, orderedPrims, refs));
                return node;
            }
        }

        // If split failed or cost was higher, fall back to creating a leaf
        int firstPrimOffset = static_cast<int>(orderedPrims.size());
        for (int i = start; i < end; ++i) {
            orderedPrims.push_back(refs[buildPrims[i].primIndex]);
        }
        node->initLeaf(firstPrimOffset, nPrims, bounds);
        return node;
    }

    node->initInterior(dim,
                       recursiveBuild(buildPrims, start, mid, totalNodes, orderedPrims, refs),
                       recursiveBuild(buildPrims, mid, end, totalNodes, orderedPrims, refs));
    return node;
}

int BVH::flattenBVHTree(BVHBuildNode* node, int* offset) {
    int myOffset = (*offset)++;
    BVHNode& linearNode = m_nodes[myOffset];
    linearNode.bounds = node->bounds;

    if (node->nPrimitives > 0) {
        linearNode.primitivesOffset = node->firstPrimOffset;
        linearNode.nPrimitives = node->nPrimitives;
    } else {
        linearNode.splitAxis = node->splitAxis;
        linearNode.nPrimitives = 0;
        flattenBVHTree(node->children[0], offset);
        linearNode.secondChildOffset = flattenBVHTree(node->children[1], offset);
    }
    return myOffset;
}

void BVH::freeBuildTree(BVHBuildNode* node) {
    if (node == nullptr) return;
    freeBuildTree(node->children[0]);
    freeBuildTree(node->children[1]);
    delete node;
}

namespace {

bool hitPrim(const PrimRef& prim, Ray& ray, SurfaceInteraction& isect) {
    if (prim.mesh) return prim.mesh->intersectTriangle(prim.tri, ray, isect);
    return prim.shape && prim.shape->intersect(ray, isect);
}

// ponytail: thread_local heap stack. A fixed 64 overflowed and dropped the branch.
// Upgrade = a bounded restart stack if you need no per-thread allocation.
std::vector<int>& traversalStack() {
    thread_local std::vector<int> stack;
    stack.clear();
    return stack;
}

} // namespace

bool BVH::intersect(Ray& ray, SurfaceInteraction& isect) const {
    if (m_nodes.empty()) return false;

    bool hit = false;
    std::vector<int>& stack = traversalStack();
    int currentNodeIndex = 0;

    Vec3f invDir(1.0f / ray.direction.x, 1.0f / ray.direction.y, 1.0f / ray.direction.z);
    int dirIsNeg[3] = { invDir.x < 0.0f ? 1 : 0, invDir.y < 0.0f ? 1 : 0, invDir.z < 0.0f ? 1 : 0 };

    while (true) {
        const BVHNode& node = m_nodes[currentNodeIndex];

        float tNear, tFar;
        if (node.bounds.intersect(ray, tNear, tFar)) {
            if (node.isLeaf()) {
                for (int i = 0; i < node.nPrimitives; ++i) {
                    if (hitPrim(m_prims[node.primitivesOffset + i], ray, isect)) hit = true;
                }
                if (stack.empty()) break;
                currentNodeIndex = stack.back();
                stack.pop_back();
            } else {
                if (dirIsNeg[node.splitAxis]) {
                    stack.push_back(currentNodeIndex + 1);
                    currentNodeIndex = node.secondChildOffset;
                } else {
                    stack.push_back(static_cast<int>(node.secondChildOffset));
                    currentNodeIndex = currentNodeIndex + 1;
                }
            }
        } else {
            if (stack.empty()) break;
            currentNodeIndex = stack.back();
            stack.pop_back();
        }
    }

    return hit;
}

bool BVH::intersectAny(const Ray& ray) const {
    if (m_nodes.empty()) return false;

    Ray localRay = ray;
    std::vector<int>& stack = traversalStack();
    int currentNodeIndex = 0;

    Vec3f invDir(1.0f / localRay.direction.x, 1.0f / localRay.direction.y, 1.0f / localRay.direction.z);
    int dirIsNeg[3] = { invDir.x < 0.0f ? 1 : 0, invDir.y < 0.0f ? 1 : 0, invDir.z < 0.0f ? 1 : 0 };

    while (true) {
        const BVHNode& node = m_nodes[currentNodeIndex];

        float tNear, tFar;
        if (node.bounds.intersect(localRay, tNear, tFar)) {
            if (node.isLeaf()) {
                SurfaceInteraction dummyIsect;
                for (int i = 0; i < node.nPrimitives; ++i) {
                    if (hitPrim(m_prims[node.primitivesOffset + i], localRay, dummyIsect)) return true;
                }
                if (stack.empty()) break;
                currentNodeIndex = stack.back();
                stack.pop_back();
            } else {
                if (dirIsNeg[node.splitAxis]) {
                    stack.push_back(currentNodeIndex + 1);
                    currentNodeIndex = node.secondChildOffset;
                } else {
                    stack.push_back(static_cast<int>(node.secondChildOffset));
                    currentNodeIndex = currentNodeIndex + 1;
                }
            }
        } else {
            if (stack.empty()) break;
            currentNodeIndex = stack.back();
            stack.pop_back();
        }
    }

    return false;
}

} // namespace photon
