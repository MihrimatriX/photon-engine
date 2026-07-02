#include "geometry/bvh.h"
#include <algorithm>
#include <iostream>

namespace photon {

void BVH::build(std::vector<std::shared_ptr<Shape>> primitives) {
    m_primitives = std::move(primitives);
    m_nodes.clear();
    m_orderedPrims.clear();

    if (m_primitives.empty()) {
        return;
    }

    std::vector<BuildPrimitive> buildPrims;
    buildPrims.reserve(m_primitives.size());
    for (size_t i = 0; i < m_primitives.size(); ++i) {
        buildPrims.emplace_back(i, m_primitives[i]->bounds());
    }

    int totalNodes = 0;
    std::vector<std::shared_ptr<Shape>> orderedPrims;
    orderedPrims.reserve(m_primitives.size());

    BVHBuildNode* root = recursiveBuild(buildPrims, 0, static_cast<int>(buildPrims.size()), &totalNodes, orderedPrims);
    m_primitives = std::move(orderedPrims); // Replace with ordered ones

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
    std::vector<std::shared_ptr<Shape>>& orderedPrims) {
    
    (*totalNodes)++;
    BVHBuildNode* node = new BVHBuildNode();

    // Compute bounds for all primitives in this node
    AABB bounds = AABB::empty();
    for (int i = start; i < end; ++i) {
        bounds = bounds.merge(buildPrims[i].bounds);
    }

    int nPrims = end - start;
    if (nPrims == 1) {
        // Create leaf node
        int firstPrimOffset = static_cast<int>(orderedPrims.size());
        for (int i = start; i < end; ++i) {
            orderedPrims.push_back(m_primitives[buildPrims[i].primIndex]);
        }
        node->initLeaf(firstPrimOffset, nPrims, bounds);
        return node;
    }

    // Compute centroid bounds
    AABB centroidBounds = AABB::empty();
    for (int i = start; i < end; ++i) {
        centroidBounds = centroidBounds.merge(buildPrims[i].centroid);
    }

    int dim = centroidBounds.maxExtent();

    // Degenerate case: all primitives have same centroid
    if (centroidBounds.pMax[dim] == centroidBounds.pMin[dim]) {
        int firstPrimOffset = static_cast<int>(orderedPrims.size());
        for (int i = start; i < end; ++i) {
            orderedPrims.push_back(m_primitives[buildPrims[i].primIndex]);
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
            buckets[b].bounds = buckets[b].bounds.merge(buildPrims[i].bounds);
        }

        // Compute cost for splitting at each bucket boundary
        float cost[nBuckets - 1];
        for (int i = 0; i < nBuckets - 1; ++i) {
            AABB b0 = AABB::empty();
            AABB b1 = AABB::empty();
            int count0 = 0;
            int count1 = 0;
            for (int j = 0; j <= i; ++j) {
                b0 = b0.merge(buckets[j].bounds);
                count0 += buckets[j].count;
            }
            for (int j = i + 1; j < nBuckets; ++j) {
                b1 = b1.merge(buckets[j].bounds);
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
                                   recursiveBuild(buildPrims, start, mid, totalNodes, orderedPrims),
                                   recursiveBuild(buildPrims, mid, end, totalNodes, orderedPrims));
                return node;
            }
        }

        // If split failed or cost was higher, fall back to creating a leaf
        int firstPrimOffset = static_cast<int>(orderedPrims.size());
        for (int i = start; i < end; ++i) {
            orderedPrims.push_back(m_primitives[buildPrims[i].primIndex]);
        }
        node->initLeaf(firstPrimOffset, nPrims, bounds);
        return node;
    }

    node->initInterior(dim,
                       recursiveBuild(buildPrims, start, mid, totalNodes, orderedPrims),
                       recursiveBuild(buildPrims, mid, end, totalNodes, orderedPrims));
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

bool BVH::intersect(Ray& ray, SurfaceInteraction& isect) const {
    if (m_nodes.empty()) return false;

    bool hit = false;
    int toVisitOffset = 0;
    int currentNodeIndex = 0;
    int nodesToVisit[64]; // Fixed-size stack for iterative traversal

    Vec3f invDir(1.0f / ray.direction.x, 1.0f / ray.direction.y, 1.0f / ray.direction.z);
    int dirIsNeg[3] = { invDir.x < 0.0f ? 1 : 0, invDir.y < 0.0f ? 1 : 0, invDir.z < 0.0f ? 1 : 0 };

    while (true) {
        const BVHNode& node = m_nodes[currentNodeIndex];

        // Check intersection with node bounds
        float tNear, tFar;
        if (node.bounds.intersect(ray, tNear, tFar)) {
            if (node.isLeaf()) {
                // Intersect primitives in leaf
                for (int i = 0; i < node.nPrimitives; ++i) {
                    if (m_primitives[node.primitivesOffset + i]->intersect(ray, isect)) {
                        hit = true;
                    }
                }
                if (toVisitOffset == 0) break;
                currentNodeIndex = nodesToVisit[--toVisitOffset];
            } else {
                // Put far child on stack, traverse near child first
                if (dirIsNeg[node.splitAxis]) {
                    nodesToVisit[toVisitOffset++] = currentNodeIndex + 1; // left child is near
                    currentNodeIndex = node.secondChildOffset;            // right child is far
                } else {
                    nodesToVisit[toVisitOffset++] = node.secondChildOffset; // right child is far
                    currentNodeIndex = currentNodeIndex + 1;                // left child is near
                }
            }
        } else {
            if (toVisitOffset == 0) break;
            currentNodeIndex = nodesToVisit[--toVisitOffset];
        }
    }

    return hit;
}

bool BVH::intersectAny(const Ray& ray) const {
    if (m_nodes.empty()) return false;

    // Temporary copy of the ray to allow updating tMax locally
    Ray localRay = ray;

    int toVisitOffset = 0;
    int currentNodeIndex = 0;
    int nodesToVisit[64];

    Vec3f invDir(1.0f / localRay.direction.x, 1.0f / localRay.direction.y, 1.0f / localRay.direction.z);
    int dirIsNeg[3] = { invDir.x < 0.0f ? 1 : 0, invDir.y < 0.0f ? 1 : 0, invDir.z < 0.0f ? 1 : 0 };

    while (true) {
        const BVHNode& node = m_nodes[currentNodeIndex];

        float tNear, tFar;
        if (node.bounds.intersect(localRay, tNear, tFar)) {
            if (node.isLeaf()) {
                SurfaceInteraction dummyIsect;
                for (int i = 0; i < node.nPrimitives; ++i) {
                    if (m_primitives[node.primitivesOffset + i]->intersect(localRay, dummyIsect)) {
                        return true; // Shadow ray hit! Fast exit
                    }
                }
                if (toVisitOffset == 0) break;
                currentNodeIndex = nodesToVisit[--toVisitOffset];
            } else {
                if (dirIsNeg[node.splitAxis]) {
                    nodesToVisit[toVisitOffset++] = currentNodeIndex + 1;
                    currentNodeIndex = node.secondChildOffset;
                } else {
                    nodesToVisit[toVisitOffset++] = node.secondChildOffset;
                    currentNodeIndex = currentNodeIndex + 1;
                }
            }
        } else {
            if (toVisitOffset == 0) break;
            currentNodeIndex = nodesToVisit[--toVisitOffset];
        }
    }

    return false;
}

} // namespace photon
