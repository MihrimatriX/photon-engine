// path_guiding.cpp — SD-Tree Path Guiding (Müller vd. 2017) uygulaması.
#include "integrators/path_guiding.h"
#include "core/math/constants.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace photon {

Vec2f guidingDirToCanonical(const Vec3f& d) {
    const Vec3f n = d.normalized();
    const float cosT = std::clamp(n.y, -1.0f, 1.0f);
    float phi = std::atan2(n.z, n.x);
    if (phi < 0.0f) phi += TWO_PI;
    return Vec2f(std::clamp(0.5f * (cosT + 1.0f), 0.0f, 0.9999999f),
                 std::clamp(phi * INV_TWO_PI, 0.0f, 0.9999999f));
}

Vec3f guidingCanonicalToDir(const Vec2f& p) {
    const float cosT = std::clamp(2.0f * p.x - 1.0f, -1.0f, 1.0f);
    const float sinT = std::sqrt(std::max(0.0f, 1.0f - cosT * cosT));
    const float phi = TWO_PI * p.y;
    return Vec3f(sinT * std::cos(phi), cosT, sinT * std::sin(phi));
}

void DirectionalTree::atomicAddFloat(std::atomic<uint32_t>& target, float delta) {
    if (!(delta > 0.0f) || !std::isfinite(delta)) return;
    uint32_t cur = target.load(std::memory_order_relaxed);
    for (;;) {
        float val = 0.0f;
        std::memcpy(&val, &cur, sizeof(float));
        const float nextVal = val + delta;
        uint32_t nextBits = 0u;
        std::memcpy(&nextBits, &nextVal, sizeof(float));
        if (target.compare_exchange_weak(cur, nextBits, std::memory_order_relaxed, std::memory_order_relaxed)) {
            return;
        }
    }
}

float DirectionalTree::loadFloat(const std::atomic<uint32_t>& target) {
    const uint32_t bits = target.load(std::memory_order_relaxed);
    float val = 0.0f;
    std::memcpy(&val, &bits, sizeof(float));
    return val;
}

DirectionalTree::DirectionalTree() {
    reset();
}

DirectionalTree::DirectionalTree(const DirectionalTree& other)
    : m_nodes(other.m_nodes)
    , m_build(other.m_build)
    , m_sum(other.m_sum)
    , m_buildSum(other.m_buildSum.load(std::memory_order_relaxed)) {}

DirectionalTree& DirectionalTree::operator=(const DirectionalTree& other) {
    if (this != &other) {
        m_nodes = other.m_nodes;
        m_build = other.m_build;
        m_sum = other.m_sum;
        m_buildSum.store(other.m_buildSum.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    return *this;
}

void DirectionalTree::reset() {
    m_nodes.clear();
    m_nodes.emplace_back();
    m_build.clear();
    m_build.emplace_back();
    m_sum = 0.0f;
    m_buildSum.store(0u, std::memory_order_relaxed);
}

void DirectionalTree::addSplat(const Vec2f& uv, float radiance) {
    if (!(radiance > 0.0f) || !std::isfinite(radiance) || m_build.empty()) return;
    atomicAddFloat(m_buildSum, radiance);
    Vec2f p = uv;
    int idx = 0;
    for (int depth = 0; depth < 16; ++depth) {
        const int qx = p.x >= 0.5f ? 1 : 0;
        const int qy = p.y >= 0.5f ? 1 : 0;
        const int q = qy * 2 + qx;
        atomicAddFloat(m_build[static_cast<size_t>(idx)].sums[q], radiance);
        const int child = m_build[static_cast<size_t>(idx)].children[q];
        if (child <= 0) break;
        p.x = p.x * 2.0f - static_cast<float>(qx);
        p.y = p.y * 2.0f - static_cast<float>(qy);
        idx = child;
    }
}

void DirectionalTree::buildFrom(const DirectionalTree& src, float splitThreshold, int maxDepth) {
    const float newSum = loadFloat(src.m_buildSum);
    if (!(newSum > 0.0f) || src.m_build.empty()) {
        // Yeni yinelemede hiç örnek gelmediyse mevcut örnekleme ağacını koru (bölünmüş yaprak soğumasın).
        if (!(m_sum > 0.0f) || m_nodes.empty()) {
            m_nodes.clear();
            m_nodes.emplace_back();
            m_sum = 0.0f;
        }
        m_build.clear();
        m_build.emplace_back();
        m_buildSum.store(0u, std::memory_order_relaxed);
        return;
    }

    m_nodes.clear();
    m_sum = newSum;

    // Örnekleme ağacını src.m_build'in atomik toplamlarından kopyala.
    m_nodes.resize(src.m_build.size());
    for (size_t i = 0; i < src.m_build.size(); ++i) {
        for (int q = 0; q < 4; ++q) {
            m_nodes[i].sums[q] = loadFloat(src.m_build[i].sums[q]);
            m_nodes[i].children[q] = src.m_build[i].children[q];
        }
    }

    // Sonraki yinelemenin topolojisini (m_build) yüksek enerjili yaprakları bölerek kur.
    const float threshold = m_sum * std::max(1e-4f, splitThreshold);
    m_build.clear();
    m_build.emplace_back();
    m_buildSum.store(0u, std::memory_order_relaxed);

    auto copyAndRefine = [&](auto&& self, int srcIdx, int dstIdx, int depth) -> void {
        for (int q = 0; q < 4; ++q) {
            const float e = m_nodes[static_cast<size_t>(srcIdx)].sums[q];
            const int srcChild = m_nodes[static_cast<size_t>(srcIdx)].children[q];
            if (e > threshold && depth < maxDepth && m_build.size() < 4096) {
                const int newChild = static_cast<int>(m_build.size());
                m_build.emplace_back();
                m_build[static_cast<size_t>(dstIdx)].children[q] = newChild;
                if (srcChild > 0) self(self, srcChild, newChild, depth + 1);
            } else {
                m_build[static_cast<size_t>(dstIdx)].children[q] = 0;
            }
        }
    };
    copyAndRefine(copyAndRefine, 0, 0, 0);
}

Vec3f DirectionalTree::sample(const Vec2f& uIn, float& pdfOut) const {
    constexpr float kInvFourPi = 1.0f / (4.0f * PI);
    if (!valid()) {
        pdfOut = kInvFourPi;
        return guidingCanonicalToDir(uIn);
    }
    Vec2f u(std::clamp(uIn.x, 0.0f, 0.9999999f), std::clamp(uIn.y, 0.0f, 0.9999999f));
    Vec2f origin(0.0f, 0.0f);
    float size = 1.0f;
    int idx = 0;
    float prob = 1.0f;

    for (int depth = 0; depth < 16; ++depth) {
        const Node& node = m_nodes[static_cast<size_t>(idx)];
        const float left = node.sums[0] + node.sums[2];
        const float right = node.sums[1] + node.sums[3];
        const float total = left + right;
        if (!(total > 0.0f)) break;

        int qx = 0;
        float px = left / total;
        if (u.x < px) {
            u.x = std::min(u.x / std::max(px, 1e-8f), 0.9999999f);
        } else {
            qx = 1;
            const float rx = std::max(1.0f - px, 1e-8f);
            u.x = std::min((u.x - px) / rx, 0.9999999f);
            px = rx;
        }

        const float bot = node.sums[qx];
        const float top = node.sums[2 + qx];
        const float colTotal = bot + top;
        int qy = 0;
        float py = colTotal > 0.0f ? bot / colTotal : 0.5f;
        if (u.y < py) {
            u.y = std::min(u.y / std::max(py, 1e-8f), 0.9999999f);
        } else {
            qy = 1;
            const float ty = std::max(1.0f - py, 1e-8f);
            u.y = std::min((u.y - py) / ty, 0.9999999f);
            py = ty;
        }

        const int q = qy * 2 + qx;
        size *= 0.5f;
        origin.x += static_cast<float>(qx) * size;
        origin.y += static_cast<float>(qy) * size;
        prob *= px * py;

        const int child = node.children[q];
        if (child <= 0) break;
        idx = child;
    }

    const Vec2f canonical(origin.x + u.x * size, origin.y + u.y * size);
    const float area = size * size;
    pdfOut = (prob / std::max(area, 1e-12f)) * kInvFourPi;
    return guidingCanonicalToDir(canonical);
}

float DirectionalTree::pdf(const Vec3f& dir) const {
    constexpr float kInvFourPi = 1.0f / (4.0f * PI);
    if (!valid()) return kInvFourPi;
    Vec2f p = guidingDirToCanonical(dir);
    float size = 1.0f;
    float prob = 1.0f;
    int idx = 0;

    for (int depth = 0; depth < 16; ++depth) {
        const Node& node = m_nodes[static_cast<size_t>(idx)];
        const float total = node.sums[0] + node.sums[1] + node.sums[2] + node.sums[3];
        if (!(total > 0.0f)) break;

        const int qx = p.x >= 0.5f ? 1 : 0;
        const int qy = p.y >= 0.5f ? 1 : 0;
        const int q = qy * 2 + qx;
        const float qSum = node.sums[q];
        if (!(qSum > 0.0f)) return 0.0f;
        prob *= qSum / total;
        size *= 0.5f;

        const int child = node.children[q];
        if (child <= 0) break;
        p.x = p.x * 2.0f - static_cast<float>(qx);
        p.y = p.y * 2.0f - static_cast<float>(qy);
        idx = child;
    }

    return (prob / std::max(size * size, 1e-12f)) * kInvFourPi;
}

GuidingField::GuidingField(const AABB& sceneBounds) {
    reset(sceneBounds);
}

void GuidingField::reset(const AABB& sceneBounds) {
    m_bounds = sceneBounds;
    if (m_bounds.pMin.x > m_bounds.pMax.x) {
        m_bounds = AABB(Vec3f(-1.0f), Vec3f(1.0f));
    } else {
        const Vec3f pad = (m_bounds.pMax - m_bounds.pMin) * 0.01f + Vec3f(1e-3f);
        m_bounds.pMin -= pad;
        m_bounds.pMax += pad;
    }
    m_nodes.clear();
    m_nodes.push_back(std::make_unique<SpatialNode>());
    // Başlangıçta 2×2×2 (8 yaprak) uzaysal ızgara: karşıt duvarlar ve tavan/zemin ilk
    // yinelemeden itibaren ayrı D-Tree'lere sahip olur.
    auto initSplit = [&](auto&& self, int idx, int depth) -> void {
        if (depth >= 3) return;
        splitLeaf(idx, depth, 12);
        const int c0 = m_nodes[static_cast<size_t>(idx)]->children[0];
        const int c1 = m_nodes[static_cast<size_t>(idx)]->children[1];
        if (c0 >= 0) self(self, c0, depth + 1);
        if (c1 >= 0) self(self, c1, depth + 1);
    };
    initSplit(initSplit, 0, 0);
    m_ready = false;
    m_iteration = 0;
}

int GuidingField::findLeaf(const Vec3f& p) const {
    if (m_nodes.empty()) return -1;
    const Vec3f extent = m_bounds.pMax - m_bounds.pMin;
    Vec3f u(
        extent.x > 1e-8f ? std::clamp((p.x - m_bounds.pMin.x) / extent.x, 0.0f, 1.0f) : 0.5f,
        extent.y > 1e-8f ? std::clamp((p.y - m_bounds.pMin.y) / extent.y, 0.0f, 1.0f) : 0.5f,
        extent.z > 1e-8f ? std::clamp((p.z - m_bounds.pMin.z) / extent.z, 0.0f, 1.0f) : 0.5f
    );
    int idx = 0;
    while (idx >= 0 && !m_nodes[static_cast<size_t>(idx)]->isLeaf) {
        const SpatialNode& n = *m_nodes[static_cast<size_t>(idx)];
        const float coord = n.axis == 0 ? u.x : (n.axis == 1 ? u.y : u.z);
        const int side = coord < n.split ? 0 : 1;
        if (side == 0) {
            if (n.axis == 0) u.x *= 2.0f;
            else if (n.axis == 1) u.y *= 2.0f;
            else u.z *= 2.0f;
        } else {
            if (n.axis == 0) u.x = u.x * 2.0f - 1.0f;
            else if (n.axis == 1) u.y = u.y * 2.0f - 1.0f;
            else u.z = u.z * 2.0f - 1.0f;
        }
        idx = n.children[side];
    }
    return idx;
}

void GuidingField::record(const Vec3f& p, const Vec3f& dir, float radiance) {
    if (!(radiance > 0.0f) || !std::isfinite(radiance)) return;
    const int leaf = findLeaf(p);
    if (leaf < 0) return;
    SpatialNode& n = *m_nodes[static_cast<size_t>(leaf)];
    n.sampleCount.fetch_add(1, std::memory_order_relaxed);
    n.building->addSplat(guidingDirToCanonical(dir), radiance);
}

void GuidingField::splitLeaf(int nodeIdx, int depth, int maxDepth) {
    if (depth >= maxDepth || m_nodes.size() >= 4096) return;
    const int axis = depth % 3;
    const int c0 = static_cast<int>(m_nodes.size());
    const int c1 = c0 + 1;
    m_nodes.push_back(std::make_unique<SpatialNode>());
    m_nodes.push_back(std::make_unique<SpatialNode>());

    SpatialNode& parent = *m_nodes[static_cast<size_t>(nodeIdx)];
    // Ebeveynin güncel örnekleme D-Tree'sini iki çocuğa aktar ki bölme sonrası ilk yineleme soğuk kalmasın.
    *m_nodes[static_cast<size_t>(c0)]->sampling = *parent.sampling;
    *m_nodes[static_cast<size_t>(c1)]->sampling = *parent.sampling;
    *m_nodes[static_cast<size_t>(c0)]->building = *parent.building;
    *m_nodes[static_cast<size_t>(c1)]->building = *parent.building;

    parent.isLeaf = false;
    parent.axis = axis;
    parent.split = 0.5f;
    parent.children[0] = c0;
    parent.children[1] = c1;
}

void GuidingField::refine(int iteration) {
    m_iteration = iteration;
    // Müller 2017: uzaysal yaprak bölme eşiği c · sqrt(2^k).
    const uint64_t splitThreshold = static_cast<uint64_t>(
        512.0 * std::sqrt(std::pow(2.0, static_cast<double>(std::min(iteration, 16)))));

    bool anyValid = false;
    // 1. Mevcut yaprakların D-Tree'lerini yenile ve yeni yineleme için sıfırla.
    std::vector<std::pair<int, int>> toSplit;
    auto walk = [&](auto&& self, int idx, int depth) -> void {
        if (idx < 0) return;
        SpatialNode& n = *m_nodes[static_cast<size_t>(idx)];
        if (n.isLeaf) {
            n.sampling->buildFrom(*n.building, 0.01f, 10);
            *n.building = *n.sampling; // yeni topoloji + sıfır toplamlar
            if (n.sampling->valid()) anyValid = true;
            const uint64_t cnt = n.sampleCount.exchange(0, std::memory_order_relaxed);
            if (cnt > splitThreshold && depth < 12) toSplit.emplace_back(idx, depth);
            return;
        }
        self(self, n.children[0], depth + 1);
        self(self, n.children[1], depth + 1);
    };
    walk(walk, 0, 0);

    // 2. Yoğun yaprakları böl.
    for (const auto& [idx, depth] : toSplit) splitLeaf(idx, depth, 12);
    m_ready = anyValid;
}

size_t GuidingField::spatialLeafCount() const {
    size_t count = 0;
    for (const auto& n : m_nodes)
        if (n && n->isLeaf) ++count;
    return count;
}

bool GuidingField::hasLeafData(const Vec3f& p) const {
    if (!m_ready) return false;
    const int leaf = findLeaf(p);
    if (leaf < 0) return false;
    return m_nodes[static_cast<size_t>(leaf)]->sampling->valid();
}

bool GuidingField::sample(const Vec3f& p, const Vec2f& u, Vec3f& wiOut, float& pdfOut) const {
    if (!m_ready) return false;
    const int leaf = findLeaf(p);
    if (leaf < 0) return false;
    const DirectionalTree& dt = *m_nodes[static_cast<size_t>(leaf)]->sampling;
    if (!dt.valid()) return false;
    wiOut = dt.sample(u, pdfOut);
    return pdfOut > 0.0f;
}

float GuidingField::pdf(const Vec3f& p, const Vec3f& wi) const {
    if (!m_ready) return 0.0f;
    const int leaf = findLeaf(p);
    if (leaf < 0) return 0.0f;
    const DirectionalTree& dt = *m_nodes[static_cast<size_t>(leaf)]->sampling;
    return dt.valid() ? dt.pdf(wi) : 0.0f;
}

} // namespace photon
