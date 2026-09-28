#include "integrators/path_tracer.h"
#include "materials/material.h"
#include "lights/light.h"
#include "lights/environment_light.h"
#include "samplers/sampler.h"
#include "engine/scene.h"
#include "core/math/frame.h"
#include "core/math/utils.h"
#include "core/sampling/sampling.h"
#include <algorithm>
#include <cmath>

namespace photon {

namespace {

Vec3f offsetRayOrigin(const Vec3f& p, const Vec3f& n, const Vec3f& dir) {
    return p + n * (dir.dot(n) > 0.0f ? 1e-4f : -1e-4f);
}

float approxAo(const Scene& scene, const SurfaceInteraction& isect, Sampler& sampler,
               float strength, int samples) {
    if (strength <= 0.0f || samples <= 0) return 1.0f;

    Frame frame(isect.normal);
    float occ = 0.0f;
    const float maxDist = 0.35f; // ponytail: short-range contact AO, not full GI
    for (int i = 0; i < samples; ++i) {
        Vec3f local = cosineSampleHemisphere(sampler.get2D());
        Vec3f dir = frame.toWorld(local).normalized();
        Ray aoRay(offsetRayOrigin(isect.point, isect.normal, dir), dir, 1e-4f, maxDist);
        if (scene.intersectAny(aoRay)) occ += 1.0f;
    }
    float visibility = 1.0f - (occ / static_cast<float>(samples));
    return lerp(1.0f, visibility, strength);
}

} // namespace

Color3f PathTracer::Li(const Ray& ray, const Scene& scene, Sampler& sampler) const {
    Color3f L = Color3f::black();
    Color3f throughput = Color3f::white();
    Ray currentRay = ray;
    bool specularBounce = true;
    float lastBsdfPdf = 0.0f;
    Vec3f lastNormal(0, 1, 0);

    // Environment participates in NEE when present (product-studio IBL).
    const EnvironmentLight* env = scene.environment();
    const auto& lights = scene.lights();
    const int explicitLights = static_cast<int>(lights.size());
    const int lightCount = explicitLights + (env ? 1 : 0);

    for (int depth = 0; depth < m_maxDepth; ++depth) {
        SurfaceInteraction isect;
        bool hit = scene.intersect(currentRay, isect);

        if (!hit) {
            if (env) {
                Color3f Le = env->eval(currentRay.direction);
                if (specularBounce || depth == 0) {
                    L += throughput * Le;
                } else {
                    // MIS: previous BSDF sample vs env as light (cosine about last hit normal)
                    float lightPdf = env->pdfLi(currentRay.direction, lastNormal);
                    float weight = powerHeuristic(1, lastBsdfPdf, 1, lightPdf);
                    L += throughput * Le * weight;
                }
            }
            break;
        }

        if (isect.material) {
            Color3f emitted = isect.material->emitted(isect);
            if (!emitted.isBlack()) {
                if (specularBounce || depth == 0) {
                    L += throughput * emitted;
                }
            }
        }

        if (!isect.material) {
            break;
        }

        // Approximate contact AO (multiplies remaining path; cheap studio polish)
        if (m_aoStrength > 0.0f && depth == 0) {
            throughput *= approxAo(scene, isect, sampler, m_aoStrength, m_shadowQuality);
        }

        bool isSpecular = isect.material->eval(-currentRay.direction, isect.normal, isect).isBlack();

        if (lightCount > 0 && !isSpecular) {
            const int shadowSamples = m_shadowQuality;
            for (int s = 0; s < shadowSamples; ++s) {
                int lightIndex = std::min(static_cast<int>(sampler.get1D() * lightCount), lightCount - 1);
                LightSample ls;
                bool fromEnv = false;

                if (lightIndex < explicitLights) {
                    ls = lights[lightIndex]->sampleLi(isect, sampler.get2D());
                    if (!ls.isValid()) continue;
                    if (lights[lightIndex]->isDelta()) {
                        // keep weight = 1 below
                    }
                } else {
                    fromEnv = true;
                    ls = env->sampleLi(isect, sampler.get2D());
                    if (!ls.isValid()) continue;
                }

                Vec3f shadowOrigin = offsetRayOrigin(isect.point, isect.normal, ls.wi);
                Ray shadowRay(shadowOrigin, ls.wi, 1e-4f, ls.distance - 1e-4f);

                if (!scene.intersectAny(shadowRay)) {
                    Color3f brdf = isect.material->eval(-currentRay.direction, ls.wi, isect);
                    float cosTheta = std::max(0.0f, ls.wi.dot(isect.normal));

                    if (!brdf.isBlack() && cosTheta > 0.0f) {
                        float weight = 1.0f;
                        bool delta = !fromEnv && lights[lightIndex]->isDelta();
                        if (!delta) {
                            float bsdfPdf = isect.material->pdf(-currentRay.direction, ls.wi, isect);
                            weight = powerHeuristic(1, ls.pdf, 1, bsdfPdf);
                        }

                        L += throughput * brdf * ls.Li * cosTheta * weight
                             * static_cast<float>(lightCount)
                             / (ls.pdf * static_cast<float>(shadowSamples));
                    }
                }
            }
        }

        Vec3f wi;
        Color3f brdf;
        float pdf;
        Vec3f wo = -currentRay.direction;

        if (!isect.material->sample(wo, isect, sampler.get2D(), wi, brdf, pdf)) {
            break;
        }

        if (pdf <= 0.0f || brdf.isBlack()) {
            break;
        }

        float cosTheta = std::abs(wi.dot(isect.normal));
        throughput *= brdf * cosTheta / pdf;
        lastBsdfPdf = pdf;
        lastNormal = isect.normal;
        specularBounce = isect.material->eval(wo, wi, isect).isBlack();

        Vec3f nextRayOrigin = offsetRayOrigin(isect.point, isect.normal, wi);
        currentRay = Ray(nextRayOrigin, wi);

        if (depth >= m_russianRouletteDepth) {
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
