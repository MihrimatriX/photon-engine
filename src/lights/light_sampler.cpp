// light_sampler.cpp — Kapsamlı ışık seçimi ve ışık BVH'si (kurulum: kovalı maliyet, PBRT-v4
// BVHLightSampler::EvaluateCost; seçim: önem oranlı iniş; pmf: bit izi).
#include "lights/light_sampler.h"
#include "core/math/constants.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

namespace photon {

namespace {

std::atomic<bool> gForceUniform{false};

constexpr float kUniformShare = 0.15f; ///< her adaya ayrılan düzgün pay (kötü tahmine sigorta)
constexpr int kBuckets = 12;
constexpr int kMaxDepth = 63;

/// Bir ışık grubunun bölme maliyeti (PBRT-v4): güç × yönsel ölçü M_Ω × kutu yüzey alanı ×
/// en-boy cezası. M_Ω, normal konisi (θo) ve yayılma (θe) ile kaplanan katı açının ağırlıklı
/// ölçüsüdür; dar koniler (aynı yöne bakan ışıklar) ucuz, her yöne bakan gruplar pahalıdır.
float groupCost(const LightBounds& b, const AABB& parent, int dim) {
    const float thetaO = std::acos(std::clamp(b.cosThetaO, -1.0f, 1.0f));
    const float thetaE = std::acos(std::clamp(b.cosThetaE, -1.0f, 1.0f));
    const float thetaW = std::min(thetaO + thetaE, PI);
    const float sinO = std::sqrt(std::max(0.0f, 1.0f - b.cosThetaO * b.cosThetaO));
    const float mOmega = TWO_PI * (1.0f - b.cosThetaO) +
                         0.5f * PI * (2.0f * thetaW * sinO - std::cos(thetaO - 2.0f * thetaW) - 2.0f * thetaO * sinO +
                                      b.cosThetaO);
    const Vec3f d = parent.diagonal();
    const float maxD = std::max({d.x, d.y, d.z});
    const float kr = d[dim] > 0.0f ? maxD / d[dim] : 1.0f;
    return b.phi * mOmega * kr * b.bounds.surfaceArea();
}

} // namespace

void LightSampler::setForceUniform(bool uniform) { gForceUniform = uniform; }

void LightSampler::build(const std::vector<const Light*>& lights, const Light* env) {
    m_lights = lights;
    m_env = env;
    m_count = static_cast<int>(lights.size()) + (env ? 1 : 0);
    m_nodes.clear();
    m_infinite.clear();
    m_topSlot.assign(static_cast<size_t>(m_count), -1);
    m_trail.assign(static_cast<size_t>(m_count), 0);
    m_depth.assign(static_cast<size_t>(m_count), 0);
    m_bvh = false;
    m_forceUniform = gForceUniform.load();
    if (m_forceUniform || m_count <= kMaxExhaustive) return;

    std::vector<BuildItem> items;
    items.reserve(lights.size());
    for (int i = 0; i < static_cast<int>(lights.size()); ++i) {
        LightBounds lb;
        if (lights[static_cast<size_t>(i)]->bounds(lb) && lb.phi > 0.0f) items.push_back({i, lb});
        else m_infinite.push_back(i);
    }
    if (env) m_infinite.push_back(static_cast<int>(lights.size()));
    // Çok sayıda sınırsız ışık (beklenmez): kapsamlı moda düşmek yerine düzgün seçim.
    if (static_cast<int>(m_infinite.size()) + 1 > kMaxInfinite) {
        m_infinite.clear();
        return;
    }
    for (size_t k = 0; k < m_infinite.size(); ++k) m_topSlot[static_cast<size_t>(m_infinite[k])] = static_cast<int16_t>(k);
    if (!items.empty()) {
        m_nodes.reserve(2 * items.size());
        buildNode(items, 0, static_cast<int>(items.size()), 0, 0);
    }
    m_bvh = true;
}

int LightSampler::buildNode(std::vector<BuildItem>& items, int begin, int end, uint64_t trail, int depth) {
    const int nodeIndex = static_cast<int>(m_nodes.size());
    m_nodes.emplace_back();
    if (end - begin == 1) {
        const BuildItem& it = items[static_cast<size_t>(begin)];
        m_nodes[static_cast<size_t>(nodeIndex)].lb = it.lb;
        m_nodes[static_cast<size_t>(nodeIndex)].light = it.index;
        m_trail[static_cast<size_t>(it.index)] = trail;
        m_depth[static_cast<size_t>(it.index)] = static_cast<uint8_t>(depth);
        return nodeIndex;
    }

    AABB bounds = AABB::empty(), centroids = AABB::empty();
    for (int i = begin; i < end; ++i) {
        bounds.merge(items[static_cast<size_t>(i)].lb.bounds);
        centroids.merge(items[static_cast<size_t>(i)].lb.bounds.centroid());
    }

    // Kovalı bölme: her eksende 12 kova, ayrımın iki yanının maliyet toplamı en küçük olan.
    float bestCost = std::numeric_limits<float>::infinity();
    int bestDim = -1, bestBucket = -1;
    if (depth < kMaxDepth - 8) {
        for (int dim = 0; dim < 3; ++dim) {
            const float lo = centroids.pMin[dim], hi = centroids.pMax[dim];
            if (!(hi > lo)) continue;
            LightBounds bucket[kBuckets];
            for (int i = begin; i < end; ++i) {
                const LightBounds& lb = items[static_cast<size_t>(i)].lb;
                int b = static_cast<int>(kBuckets * (lb.bounds.centroid()[dim] - lo) / (hi - lo));
                b = std::clamp(b, 0, kBuckets - 1);
                bucket[b] = unionBounds(bucket[b], lb);
            }
            for (int split = 0; split < kBuckets - 1; ++split) {
                LightBounds below, above;
                for (int b = 0; b <= split; ++b) below = unionBounds(below, bucket[b]);
                for (int b = split + 1; b < kBuckets; ++b) above = unionBounds(above, bucket[b]);
                if (!(below.phi > 0.0f) || !(above.phi > 0.0f)) continue;
                const float cost = groupCost(below, bounds, dim) + groupCost(above, bounds, dim);
                if (cost > 0.0f && cost < bestCost) {
                    bestCost = cost;
                    bestDim = dim;
                    bestBucket = split;
                }
            }
        }
    }

    int mid = -1;
    if (bestDim >= 0) {
        const float lo = centroids.pMin[bestDim], hi = centroids.pMax[bestDim];
        auto it = std::partition(items.begin() + begin, items.begin() + end, [&](const BuildItem& item) {
            int b = static_cast<int>(kBuckets * (item.lb.bounds.centroid()[bestDim] - lo) / (hi - lo));
            return std::clamp(b, 0, kBuckets - 1) <= bestBucket;
        });
        mid = static_cast<int>(it - items.begin());
    }
    if (mid <= begin || mid >= end) {
        // Bölünemedi (aynı yerde ışıklar) ya da derinlik sınırı: sayıca ortadan (dengeli ağaç).
        mid = (begin + end) / 2;
        const int dim = centroids.maxExtent();
        std::nth_element(items.begin() + begin, items.begin() + mid, items.begin() + end,
                         [dim](const BuildItem& a, const BuildItem& b) {
                             return a.lb.bounds.centroid()[dim] < b.lb.bounds.centroid()[dim];
                         });
    }

    const int c0 = buildNode(items, begin, mid, trail, depth + 1);
    const int c1 = buildNode(items, mid, end, trail | (uint64_t{1} << depth), depth + 1);
    Node& n = m_nodes[static_cast<size_t>(nodeIndex)];
    n.lb = unionBounds(m_nodes[static_cast<size_t>(c0)].lb, m_nodes[static_cast<size_t>(c1)].lb);
    n.child1 = c1;
    return nodeIndex;
}

LightSampler::Context LightSampler::prepare(const Vec3f& p, const Vec3f& n) const {
    Context c;
    c.m_owner = this;
    c.m_p = p;
    c.m_n = n;
    if (m_count <= 1 || m_forceUniform) return c;

    // Aday ağırlıkları: kapsamlı modda her ışık; BVH modunda sonsuz ışıklar + ağacın kendisi.
    int count;
    if (!m_bvh) {
        if (m_count > kMaxExhaustive) return c; // düzgün (çok sonsuz ışık)
        count = m_count;
        for (int i = 0; i < count; ++i) {
            const Light* l = i < static_cast<int>(m_lights.size()) ? m_lights[static_cast<size_t>(i)] : m_env;
            const float w = l->importance(p, n);
            c.m_pmf[i] = (w > 0.0f && std::isfinite(w)) ? w : 0.0f;
        }
    } else {
        count = static_cast<int>(m_infinite.size()) + (m_nodes.empty() ? 0 : 1);
        for (size_t k = 0; k < m_infinite.size(); ++k) {
            const int idx = m_infinite[k];
            const Light* l = idx < static_cast<int>(m_lights.size()) ? m_lights[static_cast<size_t>(idx)] : m_env;
            const float w = l->importance(p, n);
            c.m_pmf[k] = (w > 0.0f && std::isfinite(w)) ? w : 0.0f;
        }
        if (!m_nodes.empty()) {
            // LightBounds ölçüsü π × ışınımdır; Light::importance ışınım. Aynı birime getir.
            const float w = m_nodes[0].lb.importance(p, n) * INV_PI;
            c.m_pmf[m_infinite.size()] = (w > 0.0f && std::isfinite(w)) ? w : 0.0f;
        }
    }
    c.m_topCount = count;
    float sum = 0.0f;
    for (int i = 0; i < count; ++i) sum += c.m_pmf[i];
    if (!(sum > 0.0f) || !std::isfinite(sum)) {
        for (int i = 0; i < count; ++i) c.m_pmf[i] = 1.0f / static_cast<float>(count);
    } else {
        const float a = (1.0f - kUniformShare) / sum, b = kUniformShare / static_cast<float>(count);
        for (int i = 0; i < count; ++i) c.m_pmf[i] = c.m_pmf[i] * a + b;
    }
    c.m_uniform = false;
    return c;
}

int LightSampler::Context::sample(float u, float& pmf) const {
    pmf = 0.0f;
    if (!m_owner) return -1;
    const LightSampler& s = *m_owner;
    const int n = s.m_count;
    if (n <= 0) return -1;
    if (m_uniform) {
        pmf = 1.0f / static_cast<float>(n);
        return std::min(static_cast<int>(u * static_cast<float>(n)), n - 1);
    }
    // Üst seviye: kümülatif olasılıkla aday; seçilen aralık [0,1)'e yeniden gerilir.
    float acc = 0.0f;
    int k = m_topCount - 1;
    for (int i = 0; i < m_topCount - 1; ++i) {
        if (u < acc + m_pmf[i]) {
            k = i;
            break;
        }
        acc += m_pmf[i];
    }
    const float pk = m_pmf[k];
    if (!s.m_bvh) {
        pmf = pk;
        return k;
    }
    if (k < static_cast<int>(s.m_infinite.size())) {
        pmf = pk;
        return s.m_infinite[static_cast<size_t>(k)];
    }
    const float ur = std::min((u - acc) / pk, 0.99999994f);
    float treePmf = 0.0f;
    const int light = s.bvhSample(m_p, m_n, std::max(ur, 0.0f), treePmf);
    pmf = pk * treePmf;
    return light;
}

float LightSampler::Context::pmf(int index) const {
    if (!m_owner) return 0.0f;
    const LightSampler& s = *m_owner;
    if (index < 0 || index >= s.m_count) return 0.0f;
    if (m_uniform) return 1.0f / static_cast<float>(s.m_count);
    if (!s.m_bvh) return m_pmf[index];
    const int slot = s.m_topSlot[static_cast<size_t>(index)];
    if (slot >= 0) return m_pmf[slot];
    if (s.m_nodes.empty()) return 0.0f;
    return m_pmf[s.m_infinite.size()] * s.bvhPmf(m_p, m_n, index);
}

int LightSampler::bvhSample(const Vec3f& p, const Vec3f& n, float u, float& pmf) const {
    pmf = 1.0f;
    int node = 0;
    for (;;) {
        const Node& nd = m_nodes[static_cast<size_t>(node)];
        if (nd.light >= 0) return nd.light;
        const int c0 = node + 1, c1 = nd.child1;
        const float i0 = m_nodes[static_cast<size_t>(c0)].lb.importance(p, n);
        const float i1 = m_nodes[static_cast<size_t>(c1)].lb.importance(p, n);
        if (!(i0 > 0.0f) && !(i1 > 0.0f)) {
            pmf = 0.0f;
            return -1;
        }
        const float p0 = i0 / (i0 + i1);
        if (u < p0) {
            node = c0;
            u = std::min(u / p0, 0.99999994f);
            pmf *= p0;
        } else {
            node = c1;
            u = std::min((u - p0) / (1.0f - p0), 0.99999994f);
            pmf *= 1.0f - p0;
        }
    }
}

float LightSampler::bvhPmf(const Vec3f& p, const Vec3f& n, int index) const {
    if (index < 0 || index >= static_cast<int>(m_trail.size())) return 0.0f;
    uint64_t trail = m_trail[static_cast<size_t>(index)];
    const int depth = m_depth[static_cast<size_t>(index)];
    float pmf = 1.0f;
    int node = 0;
    for (int d = 0; d < depth; ++d) {
        const Node& nd = m_nodes[static_cast<size_t>(node)];
        if (nd.light >= 0) break;
        const int c0 = node + 1, c1 = nd.child1;
        const float i0 = m_nodes[static_cast<size_t>(c0)].lb.importance(p, n);
        const float i1 = m_nodes[static_cast<size_t>(c1)].lb.importance(p, n);
        const float sum = i0 + i1;
        if (!(sum > 0.0f)) return 0.0f;
        if (trail & 1u) {
            pmf *= i1 / sum;
            node = c1;
        } else {
            pmf *= i0 / sum;
            node = c0;
        }
        trail >>= 1;
    }
    const Node& leaf = m_nodes[static_cast<size_t>(node)];
    return leaf.light == index ? pmf : 0.0f;
}

} // namespace photon
