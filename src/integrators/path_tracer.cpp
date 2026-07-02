#include "integrators/path_tracer.h"
#include "materials/material.h"
#include "lights/light.h"
#include "samplers/sampler.h"
#include "engine/scene.h"
#include "core/math/utils.h"
#include <algorithm>
#include <cmath>

namespace photon {

Color3f PathTracer::Li(const Ray& ray, const Scene& scene, Sampler& sampler) const {
    Color3f L = Color3f::black();
    Color3f throughput = Color3f::white();
    Ray currentRay = ray;
    bool specularBounce = true; // True for camera rays and specular reflections

    for (int depth = 0; depth < m_maxDepth; ++depth) {
        SurfaceInteraction isect;
        bool hit = scene.intersect(currentRay, isect);

        // If we miss, sample environment map (sky background)
        if (!hit) {
            if (scene.environment()) {
                L += throughput * scene.environment()->eval(currentRay.direction);
            }
            break;
        }

        // Add emitted light (only from light sources directly intersected,
        // or if the bounce was specular to avoid double-counting with Next Event Estimation)
        if (isect.material) {
            Color3f emitted = isect.material->emitted(isect);
            if (!emitted.isBlack()) {
                if (specularBounce || depth == 0) {
                    L += throughput * emitted;
                }
            }
        }

        if (!isect.material) {
            break; // Shading error: no material found
        }

        const auto& lights = scene.lights();
        
        // Direct Lighting (Next Event Estimation)
        // Only evaluate direct lighting if the surface is not perfectly specular
        bool isSpecular = isect.material->eval(-currentRay.direction, isect.normal, isect).isBlack();
        
        if (!lights.empty() && !isSpecular) {
            // Pick a light source uniformly at random
            int lightCount = static_cast<int>(lights.size());
            int lightIndex = std::min(static_cast<int>(sampler.get1D() * lightCount), lightCount - 1);
            const Light* light = lights[lightIndex];

            LightSample ls = light->sampleLi(isect, sampler.get2D());
            
            if (ls.isValid()) {
                // Trace shadow ray to check for occlusion
                // Offset origin along normal to avoid self-intersection
                Vec3f shadowOrigin = isect.point + isect.normal * (ls.wi.dot(isect.normal) > 0.0f ? 1e-4f : -1e-4f) * isect.normal;
                Ray shadowRay(shadowOrigin, ls.wi, 1e-4f, ls.distance - 1e-4f);
                
                if (!scene.intersectAny(shadowRay)) {
                    // Evaluate BRDF for direct lighting direction
                    Color3f brdf = isect.material->eval(-currentRay.direction, ls.wi, isect);
                    float cosTheta = std::max(0.0f, ls.wi.dot(isect.normal));
                    
                    if (!brdf.isBlack() && cosTheta > 0.0f) {
                        float weight = 1.0f;
                        // Multiple Importance Sampling (only for non-delta lights)
                        if (!light->isDelta()) {
                            float bsdfPdf = isect.material->pdf(-currentRay.direction, ls.wi, isect);
                            weight = powerHeuristic(1, ls.pdf, 1, bsdfPdf);
                        }
                        
                        L += throughput * brdf * ls.Li * cosTheta * weight * static_cast<float>(lightCount) / ls.pdf;
                    }
                }
            }
        }

        // Sample BRDF for the next bounce direction
        Vec3f wi;
        Color3f brdf;
        float pdf;
        Vec3f wo = -currentRay.direction;

        if (!isect.material->sample(wo, isect, sampler.get2D(), wi, brdf, pdf)) {
            break; // Ray is absorbed or invalid sample
        }

        if (pdf <= 0.0f || brdf.isBlack()) {
            break;
        }

        float cosTheta = std::abs(wi.dot(isect.normal));
        throughput *= brdf * cosTheta / pdf;

        // Check if the bounce was specular (delta distribution)
        // If eval returns black, it's a delta bounce (e.g. perfect mirror/dielectric reflection)
        specularBounce = isect.material->eval(wo, wi, isect).isBlack();

        // Setup the ray for the next bounce
        Vec3f nextRayOrigin = isect.point + isect.normal * (wi.dot(isect.normal) > 0.0f ? 1e-4f : -1e-4f) * isect.normal;
        currentRay = Ray(nextRayOrigin, wi);

        // Russian Roulette path termination
        if (depth >= m_russianRouletteDepth) {
            // Keep going with probability proportional to throughput luminance
            float q = std::max(0.05f, 1.0f - throughput.luminance());
            if (sampler.get1D() < q) {
                break;
            }
            throughput /= (1.0f - q);
        }
    }

    return L;
}

} // namespace photon
