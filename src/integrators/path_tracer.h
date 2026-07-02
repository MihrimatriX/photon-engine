#pragma once

/// @file path_tracer.h
/// @brief Multi-bounce Path Tracer integrator in PhotonEngine.

#include "integrators/integrator.h"

namespace photon {

/// @brief Path tracer supporting global illumination, direct lighting, and Russian roulette.
class PathTracer : public Integrator {
public:
    explicit PathTracer(int maxDepth = 8, int russianRouletteDepth = 3)
        : m_maxDepth(maxDepth), m_russianRouletteDepth(russianRouletteDepth) {}

    Color3f Li(const Ray& ray, const Scene& scene, Sampler& sampler) const override;

private:
    int m_maxDepth;
    int m_russianRouletteDepth;
};

} // namespace photon
