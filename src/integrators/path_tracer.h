// Yol izleyici integratörün arayüzü. maxDepth = en fazla saçılma (sekme) sayısı
// (PBRT kuralı): 1 → yalnızca doğrudan ışık, 2 → bir sekme dolaylı ışık, ...
#pragma once

/// @file path_tracer.h
/// @brief Multi-bounce Path Tracer integrator in PhotonEngine.

#include "integrators/integrator.h"
#include "geometry/surface_interaction.h"

namespace photon {

/// @brief Path tracer supporting global illumination, direct lighting, and Russian roulette.
/// @p maxDepth counts scattering events; emission found by the last scattered ray is still added.
class PathTracer : public Integrator {
public:
    explicit PathTracer(int maxDepth = 8, int russianRouletteDepth = 3,
                        float aoStrength = 0.0f, int shadowQuality = 1)
        : m_maxDepth(maxDepth)
        , m_russianRouletteDepth(russianRouletteDepth)
        , m_aoStrength(aoStrength)
        , m_shadowQuality(shadowQuality < 1 ? 1 : shadowQuality) {}

    /// Kamera ışınının ilk kesişimi (gürültü giderici AOV'leri ve seçim için).
    struct PrimaryHit {
        bool hit = false;
        SurfaceInteraction isect;
    };

    Color3f Li(const Ray& ray, const Scene& scene, Sampler& sampler) const override {
        return Li(ray, scene, sampler, nullptr);
    }

    /// @p ray bir KAMERA ışınıysa ve @p primary verilmişse ilk kesişim oraya yazılır,
    /// sahnenin arka plan modu (düz renk / şeffaf) ıskalayan kamera ışınına uygulanır.
    /// Böylece AOV için ayrı bir birincil ışın atmaya gerek kalmaz.
    Color3f Li(const Ray& ray, const Scene& scene, Sampler& sampler, PrimaryHit* primary) const;

private:
    int m_maxDepth;
    int m_russianRouletteDepth;
    float m_aoStrength;
    int m_shadowQuality;
};

} // namespace photon
