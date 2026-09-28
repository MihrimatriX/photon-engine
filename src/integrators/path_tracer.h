#pragma once

/// @file path_tracer.h
/// @brief Multi-bounce Path Tracer integrator in PhotonEngine.

#include "integrators/integrator.h"

namespace photon {

/// @brief Path tracer supporting global illumination, direct lighting, and Russian roulette.
class PathTracer : public Integrator {
public:
    explicit PathTracer(int maxDepth = 8, int russianRouletteDepth = 3,
                        float aoStrength = 0.0f, int shadowQuality = 1)
        : m_maxDepth(maxDepth)
        , m_russianRouletteDepth(russianRouletteDepth)
        , m_aoStrength(aoStrength)
        , m_shadowQuality(shadowQuality < 1 ? 1 : shadowQuality) {}

    Color3f Li(const Ray& ray, const Scene& scene, Sampler& sampler) const override;

private:
    int m_maxDepth;
    int m_russianRouletteDepth;
    float m_aoStrength;
    int m_shadowQuality;
};

} // namespace photon
