// path_guiding.h — SD-Tree Path Guiding (Müller vd. 2017, "Practical Path Guiding for Efficient
// Light-Transport Simulation").
//
// Sahne hacmini uyarlamalı uzaysal ikili ağaçla (STree / kD-tree) böler; her uzaysal yaprak
// [0,1]² silindirik eş-alan küre izdüşümü üzerinde bir yönlü dörtlü ağaç (DTree) taşır.
// Eğitim geçişlerinde iş parçacığı güvenli atomik radyans biriktirilir; refine() ardından
// yüksek enerjili bölgeler bölünür ve örnekleme ağacı (sampling) BSDF ile 1-örnekli MIS
// karışımında (0.5·p_bsdf + 0.5·p_guide) kullanılır. Böylece kostikler ve dar deliklerden
// gelen dolaylı ışıkta varyans belirgin biçimde düşer ve tahminci %100 yansız kalır.
#pragma once

#include "core/math/aabb.h"
#include "core/math/vec.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace photon {

/// Eş-alan küre ↔ [0, 1]² silindirik izdüşümü (sabit Jakobiyen: 1 / (4π)).
Vec2f guidingDirToCanonical(const Vec3f& d);
Vec3f guidingCanonicalToDir(const Vec2f& p);

/// Tek bir yönlü dörtlü ağaç (D-Tree): [0, 1]² üzerinde parçalı sabit yoğunluk.
class DirectionalTree {
public:
    DirectionalTree();
    DirectionalTree(const DirectionalTree& other);
    DirectionalTree& operator=(const DirectionalTree& other);

    void reset();
    void addSplat(const Vec2f& uv, float radiance);
    void buildFrom(const DirectionalTree& src, float splitThreshold = 0.01f, int maxDepth = 10);

    bool valid() const { return m_sum > 0.0f && !m_nodes.empty(); }
    float totalEnergy() const { return m_sum; }
    size_t nodeCount() const { return m_nodes.size(); }

    /// D-Tree dağılımından birim yön ve katı açı (sr⁻¹) PDF'i üretir.
    Vec3f sample(const Vec2f& u, float& pdfOut) const;
    /// Verilen birim yön için D-Tree katı açı PDF'i (sr⁻¹).
    float pdf(const Vec3f& dir) const;

private:
    struct Node {
        float sums[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        int children[4] = {0, 0, 0, 0}; ///< 0 = yaprak
    };

    // Yazma sırasında lock-free atomik birikim için ayrı düğüm dizisi.
    struct AtomicNode {
        std::atomic<uint32_t> sums[4];
        int children[4] = {0, 0, 0, 0};

        AtomicNode() {
            for (int i = 0; i < 4; ++i) sums[i].store(0u, std::memory_order_relaxed);
        }
        AtomicNode(const AtomicNode& o) {
            for (int i = 0; i < 4; ++i) {
                sums[i].store(o.sums[i].load(std::memory_order_relaxed), std::memory_order_relaxed);
                children[i] = o.children[i];
            }
        }
        AtomicNode& operator=(const AtomicNode& o) {
            if (this != &o) {
                for (int i = 0; i < 4; ++i) {
                    sums[i].store(o.sums[i].load(std::memory_order_relaxed), std::memory_order_relaxed);
                    children[i] = o.children[i];
                }
            }
            return *this;
        }
    };

    static void atomicAddFloat(std::atomic<uint32_t>& target, float delta);
    static float loadFloat(const std::atomic<uint32_t>& target);

    std::vector<Node> m_nodes;          ///< Salt-okunur örnekleme ağacı
    std::vector<AtomicNode> m_build;    ///< Atomik eğitim ağacı
    float m_sum = 0.0f;
    std::atomic<uint32_t> m_buildSum{0u};
};

/// Sahne genelinde SD-Tree Path Guiding alanı.
class GuidingField {
public:
    GuidingField() = default;
    explicit GuidingField(const AABB& sceneBounds);

    /// Sahne sınırlarıyla sıfırlar (önceki eğitim silinir).
    void reset(const AABB& sceneBounds);

    /// Eğitim verisi kaydeder (tüm iş parçacıklarından eşzamanlı çağrılabilir).
    void record(const Vec3f& p, const Vec3f& dir, float radiance);

    /// Bir eğitim yinelemesini tamamlar: D-Tree'leri yeniler, yoğun uzaysal yaprakları böler
    /// ve sonraki yinelemenin birikim tamponlarını sıfırlar.
    void refine(int iteration);

    /// En az bir kez refine() edilmiş ve geçerli enerjiye sahip mi?
    bool ready() const { return m_ready; }
    /// @p p noktasının bulunduğu uzaysal yaprakta geçerli yönlü dağılım var mı?
    bool hasLeafData(const Vec3f& p) const;
    int iteration() const { return m_iteration; }
    size_t spatialLeafCount() const;

    /// @p p noktasında yön ve katı açı PDF'i örnekler. Veri yoksa false döner.
    bool sample(const Vec3f& p, const Vec2f& u, Vec3f& wiOut, float& pdfOut) const;
    /// @p p noktasında @p wi yönünün kılavuzlama PDF'i (sr⁻¹). Veri yoksa 0.
    float pdf(const Vec3f& p, const Vec3f& wi) const;

private:
    struct SpatialNode {
        bool isLeaf = true;
        int axis = 0;
        float split = 0.5f;
        int children[2] = {-1, -1};
        std::unique_ptr<DirectionalTree> sampling;
        std::unique_ptr<DirectionalTree> building;
        std::atomic<uint64_t> sampleCount{0};

        SpatialNode()
            : sampling(std::make_unique<DirectionalTree>())
            , building(std::make_unique<DirectionalTree>()) {}
    };

    int findLeaf(const Vec3f& p) const;
    void splitLeaf(int nodeIdx, int depth, int maxDepth);

    AABB m_bounds = AABB::empty();
    std::vector<std::unique_ptr<SpatialNode>> m_nodes;
    bool m_ready = false;
    int m_iteration = 0;
};

} // namespace photon
