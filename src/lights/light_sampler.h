// light_sampler.h — Işık seçimi: bir yüzey noktasında hangi ışığa gölge ışını atılacağı.
//
// İki mod:
//  * Kapsamlı (≤ 33 aday): her ışığın o noktaya tahmini katkısı (Light::importance) hesaplanır,
//    olasılık %85 orantılı + %15 düzgün tabandır. Az ışıkta en isabetli seçim (vitrin sahnesinde
//    düzgün seçime göre relMSE yarıya inmişti).
//  * Işık BVH'si (çok ışık): Conty & Kulla 2018 / PBRT-v4 BVHLightSampler. Sınırlı ışıklar
//    (alan, üçgen, nokta, spot) güç + yön konisi özetli bir ağaçta toplanır; seçim kökten yaprağa
//    iner, her düğümde iki çocuktan biri LightBounds::importance oranında seçilir. Maliyet
//    O(log N): binlerce yayıcı üçgenli LED şerit, neon tabela ya da ekran da temiz örneklenir.
//    Sonsuz ışıklar (ortam, güneş) ağacın dışında, ağacın kendisiyle birlikte en üstte seçilir.
//
// MIS için aynı dağılımın pmf'i lazım: BSDF ışını bir ışığa çarpınca önceki köşenin bağlamıyla
// pmf(i) hesaplanır. BVH'de ışığın kökten yaprağa giden yolu (bit izi) saklanır; pmf O(derinlik).
#pragma once

#include "lights/light.h"
#include "lights/light_bounds.h"

#include <cstdint>
#include <vector>

namespace photon {

class LightSampler {
public:
    /// Kapsamlı moddaki en fazla aday (ışık + ortam). Üstünde ışık BVH'si.
    static constexpr int kMaxExhaustive = 33;
    static constexpr int kMaxInfinite = 16;

    /// @p lights sahnenin ışıkları (Scene::lights() sırası), @p env ortam ışığı (yoksa nullptr).
    /// Ortamın aday sırası lights.size()'dır.
    void build(const std::vector<const Light*>& lights, const Light* env);

    int candidateCount() const { return m_count; }
    bool usesBvh() const { return m_bvh; }

    /// Bir köşeye özgü seçim dağılımı. prepare() ile kurulur, aynı köşede örnekleme ve sonraki
    /// isabetin MIS ağırlığı için tutulur.
    class Context {
    public:
        /// u ∈ [0,1) → aday sırası (ortam = ışık sayısı); seçilemezse −1. @p pmf olasılık.
        int sample(float u, float& pmf) const;
        /// @p index adayının bu köşeden seçilme olasılığı.
        float pmf(int index) const;

    private:
        friend class LightSampler;
        const LightSampler* m_owner = nullptr;
        Vec3f m_p, m_n;
        bool m_uniform = true;
        // Kapsamlı mod: aday başına olasılık. BVH modu: en üst seviye (sonsuz ışıklar + ağaç).
        float m_pmf[kMaxExhaustive]{};
        int m_topCount = 0;
    };

    Context prepare(const Vec3f& p, const Vec3f& n) const;

    /// Ölçüm/test kancası: tüm seçimler düzgün olsun (eski çok-ışık davranışı). Kurulumda okunur.
    static void setForceUniform(bool uniform);

private:
    struct Node {
        LightBounds lb;
        int32_t child1 = -1; ///< iç düğüm: ikinci çocuk (ilki hemen sonraki düğüm)
        int32_t light = -1;  ///< yaprak: ışığın aday sırası
    };
    struct BuildItem {
        int index;
        LightBounds lb;
    };

    int buildNode(std::vector<BuildItem>& items, int begin, int end, uint64_t trail, int depth);
    float bvhPmf(const Vec3f& p, const Vec3f& n, int index) const;
    int bvhSample(const Vec3f& p, const Vec3f& n, float u, float& pmf) const;

    std::vector<const Light*> m_lights;
    const Light* m_env = nullptr;
    int m_count = 0;
    bool m_bvh = false;
    bool m_forceUniform = false;

    // BVH modu
    std::vector<Node> m_nodes;
    std::vector<int> m_infinite;          ///< sınırsız adaylar (ortam dahil), en üst seviye sırası
    std::vector<int16_t> m_topSlot;       ///< aday → en üst seviye yuvası (−1: ağaçta)
    std::vector<uint64_t> m_trail;        ///< aday → kökten yaprağa sol/sağ bitleri
    std::vector<uint8_t> m_depth;
};

} // namespace photon
