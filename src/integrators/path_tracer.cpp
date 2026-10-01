#include "integrators/path_tracer.h"
#include "materials/material.h"
#include "lights/light.h"
#include "lights/area_light.h"
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

float areaLightSolidAnglePdf(const std::vector<const Light*>& lights,
                             const Vec3f& ref, const Vec3f& pLight) {
    for (const Light* light : lights) {
        if (const auto* area = dynamic_cast<const AreaLight*>(light)) {
            float pdf = area->pdfLi(ref, pLight);
            if (pdf > 0.0f) return pdf;
        } else if (const auto* mesh = dynamic_cast<const MeshLight*>(light)) {
            float pdf = mesh->pdfLi(ref, pLight);
            if (pdf > 0.0f) return pdf;
        }
    }
    return 0.0f;
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
    Vec3f lastPoint = ray.origin;

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
                    // Camera ray and delta bounce: the BSDF has no matching NEE sample.
                    L += throughput * emitted;
                } else {
                    // Diffuse/glossy hit of an emissive quad. Complementary to NEE's
                    // powerHeuristic(lightPdf, bsdfPdf). Env MIS is unchanged (miss path).
                    float lightPdf = areaLightSolidAnglePdf(lights, lastPoint, isect.point);
                    float weight = 1.0f;
                    if (lightPdf > 0.0f && lastBsdfPdf > 0.0f) {
                        weight = powerHeuristic(1, lastBsdfPdf, 1, lightPdf);
                    }
                    L += throughput * emitted * weight;
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
                Vec3f shadowDir = ls.wi;
                float shadowEnd = ls.distance - 1e-4f;
                if (!fromEnv && !lights[lightIndex]->isDelta()) {
                    // Origin is pushed off the receiver, so a ray along wi meets the
                    // light plane early and the quad occludes its own NEE sample.
                    Vec3f pLight = isect.point + ls.wi * ls.distance;
                    Vec3f delta = pLight - shadowOrigin;
                    float dist = delta.length();
                    if (dist > 1e-4f) {
                        shadowDir = delta / dist;
                        // ponytail: stop short of the sample; a contact occluder inside the slop is missed until shadow rays skip light prims
                        float slop = std::max(1e-4f, dist * 1e-4f);
                        shadowEnd = dist - slop;
                    }
                }

                if (shadowEnd <= 1e-4f) continue;
                Ray shadowRay(shadowOrigin, shadowDir, 1e-4f, shadowEnd);

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
        lastPoint = isect.point;
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
