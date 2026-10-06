// bvh.cpp — BVH yapımı (12 kovalı SAH, düzleştirilmiş 32 baytlık düğümler) ve gezinme:
// en yakın isabet için yakın-çocuk-önce sıralı gezinme, gölge ışınları için erken çıkış.
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
    m_tris.resize(m_prims.size());
    for (size_t i = 0; i < m_prims.size(); ++i) {
        if (!m_prims[i].mesh) continue;
        Vec3f p0, p1, p2;
        m_prims[i].mesh->triangleVertices(m_prims[i].tri, p0, p1, p2);
        // Kenarlar Triangle::intersect ile AYNI işlemle hesaplanır: aday testi ile
        // son gölgelendirme hesabı bit bit aynı t değerini bulur.
        const Vec3f e1 = p1 - p0, e2 = p2 - p0;
        m_tris[i] = TriAccel{p0, e1, e2, 1e-14f * e1.lengthSquared() * e2.lengthSquared()};
    }

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
        // Kovalı SAH (Surface Area Heuristic, yüzey alanı sezgiseli). Rastgele bir ışının
        // bir kutuya çarpma olasılığı kutunun yüzey alanıyla orantılıdır. Bir bölmenin
        // beklenen maliyeti: C = C_gezinme + (A_sol/A)·N_sol + (A_sağ/A)·N_sağ.
        // Merkezler en uzun eksende 12 kovaya dağıtılır, 11 kesim noktası denenir ve
        // en ucuzu seçilir; yaprak maliyetinden (N) pahalıysa yaprak yapılır.
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
        linearNode.nPrimitives = static_cast<uint16_t>(node->nPrimitives);
    } else {
        linearNode.splitAxis = static_cast<uint8_t>(node->splitAxis);
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

// Sınırlayıcı kutu ile ışın (slab yöntemi). 1/d önceden hesaplanır: düğüm başına
// bölme yok. Her eksen için ışının kutuya girdiği ve çıktığı t değerleri bulunur;
// en geç giriş, en erken çıkıştan küçükse kesişim vardır. tNear: giriş mesafesi.
inline bool hitBox(const AABB& b, const Vec3f& o, const Vec3f& inv, float tMin, float tMax, float& tNear) {
    float tx0 = (b.pMin.x - o.x) * inv.x, tx1 = (b.pMax.x - o.x) * inv.x;
    if (tx0 > tx1) std::swap(tx0, tx1);
    float ty0 = (b.pMin.y - o.y) * inv.y, ty1 = (b.pMax.y - o.y) * inv.y;
    if (ty0 > ty1) std::swap(ty0, ty1);
    float tz0 = (b.pMin.z - o.z) * inv.z, tz1 = (b.pMax.z - o.z) * inv.z;
    if (tz0 > tz1) std::swap(tz0, tz1);
    // NaN (0·∞) her karşılaştırmada false döner; std::max(a, NaN) = a olduğu için
    // NaN olabilecek değerler ikinci argümanda tutulur. Çıkış biraz büyütülür
    // (pbrt'deki 1 + 2γ3 payı): kayan nokta yuvarlaması ince kutuları kaçırmasın.
    const float t0 = std::max(std::max(tMin, tx0), std::max(ty0, tz0));
    const float t1 = std::min(std::min(tMax, tx1), std::min(ty1, tz1)) * 1.0000004f;
    tNear = t0;
    return t0 <= t1;
}

// ponytail: thread_local yığın; derinlik sınırı yok. Sabit dizi + derinlik sınırlı
// yapım daha hızlı olur (Faz 6, TLAS/BLAS ile birlikte).
struct StackEntry {
    int node;
    float tNear;
};

std::vector<StackEntry>& traversalStack() {
    thread_local std::vector<StackEntry> stack;
    stack.clear();
    return stack;
}

} // namespace

// Yalın Möller–Trumbore: yalnız t (u, v kontrol için). Triangle::intersect ile aynı
// işlemler ve aynı eşik; böylece en yakın isabet için yapılan tam hesap aynı t'yi bulur.
bool BVH::hitTriangleLean(uint32_t i, const Ray& r, float& t) const {
    const TriAccel& T = m_tris[i];
    const Vec3f h = r.direction.cross(T.e2);
    const float a = T.e1.dot(h);
    if (a * a <= T.parallelEps) return false;
    const float f = 1.0f / a;
    const Vec3f s = r.origin - T.v0;
    const float u = f * s.dot(h);
    if (!(u >= 0.0f && u <= 1.0f)) return false;
    const Vec3f q = s.cross(T.e1);
    const float v = f * r.direction.dot(q);
    if (!(v >= 0.0f && u + v <= 1.0f)) return false;
    t = f * T.e2.dot(q);
    return t >= r.tMin && t <= r.tMax;
}

// En yakın kesişim. Önce yakın çocuk gezilir, uzak çocuk giriş mesafesiyle yığına
// konur; yığından alınırken o mesafe şimdiye dek bulunan isabetten uzaksa atlanır.
// Adaylarda yalnız yalın üçgen testi yapılır; normal, UV ve teğet gibi gölgelendirme
// verisi döngü bitince yalnız en yakın üçgen için bir kez hesaplanır.
bool BVH::intersect(Ray& ray, SurfaceInteraction& isect) const {
    if (m_nodes.empty()) return false;
    const Vec3f inv(1.0f / ray.direction.x, 1.0f / ray.direction.y, 1.0f / ray.direction.z);
    std::vector<StackEntry>& stack = traversalStack();

    int bestTri = -1;       // en yakın mesh üçgeninin m_prims indeksi
    bool shapeHit = false;  // en yakın isabet bir küre / tek üçgen şekli mi (isect dolu)
    int node = 0;
    float tNear = 0.0f;
    if (!hitBox(m_nodes[0].bounds, ray.origin, inv, ray.tMin, ray.tMax, tNear)) return false;

    while (true) {
        const BVHNode& n = m_nodes[static_cast<size_t>(node)];
        if (n.isLeaf()) {
            for (int k = 0; k < n.nPrimitives; ++k) {
                const uint32_t pi = n.primitivesOffset + static_cast<uint32_t>(k);
                const PrimRef& p = m_prims[pi];
                if (p.mesh) {
                    float t;
                    if (hitTriangleLean(pi, ray, t)) {
                        ray.tMax = t;
                        bestTri = static_cast<int>(pi);
                        shapeHit = false;
                    }
                } else if (p.shape && p.shape->intersect(ray, isect)) {
                    isect.hitObject = p.shape;
                    bestTri = -1;
                    shapeHit = true;
                }
            }
        } else {
            const int c0 = node + 1;
            const int c1 = static_cast<int>(n.secondChildOffset);
            float t0, t1;
            const bool h0 = hitBox(m_nodes[static_cast<size_t>(c0)].bounds, ray.origin, inv, ray.tMin, ray.tMax, t0);
            const bool h1 = hitBox(m_nodes[static_cast<size_t>(c1)].bounds, ray.origin, inv, ray.tMin, ray.tMax, t1);
            if (h0 && h1) {
                if (t1 < t0) {
                    stack.push_back({c0, t0});
                    node = c1;
                } else {
                    stack.push_back({c1, t1});
                    node = c0;
                }
                continue;
            }
            if (h0) { node = c0; continue; }
            if (h1) { node = c1; continue; }
        }
        // Yığından sıradaki düğüm; bulunan isabetten uzak olanlar atlanır.
        bool found = false;
        while (!stack.empty()) {
            const StackEntry e = stack.back();
            stack.pop_back();
            if (e.tNear <= ray.tMax) {
                node = e.node;
                found = true;
                break;
            }
        }
        if (!found) break;
    }

    if (bestTri >= 0) {
        const PrimRef& p = m_prims[static_cast<size_t>(bestTri)];
        const float bestT = ray.tMax;
        if (!p.mesh->intersectTriangle(p.tri, ray, isect)) {
            // Olmaması gerekir (aynı işlemler); yine de yuvarlamaya karşı payla dene.
            ray.tMax = bestT * 1.000001f + 1e-7f;
            if (!p.mesh->intersectTriangle(p.tri, ray, isect)) return false;
        }
        isect.hitObject = p.mesh;
        return true;
    }
    return shapeHit;
}

// Gölge ışını: herhangi bir isabette hemen dön; gölgelendirme verisi hiç hesaplanmaz.
bool BVH::intersectAny(const Ray& ray) const {
    if (m_nodes.empty()) return false;
    const Vec3f inv(1.0f / ray.direction.x, 1.0f / ray.direction.y, 1.0f / ray.direction.z);
    const bool dirIsNeg[3] = {inv.x < 0.0f, inv.y < 0.0f, inv.z < 0.0f};
    std::vector<StackEntry>& stack = traversalStack();
    int node = 0;
    float tNear;
    while (true) {
        const BVHNode& n = m_nodes[static_cast<size_t>(node)];
        if (hitBox(n.bounds, ray.origin, inv, ray.tMin, ray.tMax, tNear)) {
            if (n.isLeaf()) {
                for (int k = 0; k < n.nPrimitives; ++k) {
                    const uint32_t pi = n.primitivesOffset + static_cast<uint32_t>(k);
                    const PrimRef& p = m_prims[pi];
                    float t;
                    if (p.mesh) {
                        if (hitTriangleLean(pi, ray, t)) return true;
                    } else if (p.shape) {
                        Ray r = ray;
                        SurfaceInteraction dummy;
                        if (p.shape->intersect(r, dummy)) return true;
                    }
                }
            } else {
                // Gölge ışınında sıra önemsiz; yön işaretine göre yakın olan önce.
                if (dirIsNeg[n.splitAxis]) {
                    stack.push_back({node + 1, 0.0f});
                    node = static_cast<int>(n.secondChildOffset);
                } else {
                    stack.push_back({static_cast<int>(n.secondChildOffset), 0.0f});
                    node = node + 1;
                }
                continue;
            }
        }
        if (stack.empty()) break;
        node = stack.back().node;
        stack.pop_back();
    }
    return false;
}

} // namespace photon
