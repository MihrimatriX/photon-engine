// Çok sekmeli yol izleyici (path tracer): her yüzey noktasında ışık örneklemesi (NEE)
// ve BSDF örneklemesi yapılır, ikisi Veach'in güç sezgiseli (MIS) ile birleştirilir.
// Rus ruleti, NaN/Inf koruması ve isteğe bağlı kontak AO da burada.

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

/// Bir kamera ışını boyunca gelen radyansın tek örnekli tahmini.
///
/// MIS (Veach 1997 §9.2): Aynı ışık yolu iki stratejiyle bulunabilir:
///   (a) NEE: ışıktan bir nokta seç, pdf_l = P(ışık seçimi) · pdf_ışık(ω)
///   (b) BSDF: BSDF'den yön seç ve ışığa çarp, pdf_b = pdf_bsdf(ω)
/// Her katkı güç sezgiseli w = (n_a p_a)² / ((n_a p_a)² + (n_b p_b)²) ile tartılır;
/// iki ağırlığın toplamı 1 olduğu için hiçbir şey iki kez sayılmaz. Bunun için İKİ
/// tarafta da AYNI pdf_l kullanılmalıdır: ışık seçim olasılığı (1/ışıkSayısı) ve NEE
/// örnek sayısı (shadowSamples) her iki ağırlıkta da vardır (bulgu lighting-9).
///
/// Derinlik kuralı (PBRT ile aynı): m_maxDepth = en fazla saçılma (sekme) sayısı.
/// Son saçılmada NEE yapılır ve BSDF yönü de izlenir; o yöndeki ışık yayımı bir sonraki
/// turda MIS ağırlığıyla eklenip döngü biter. Eskiden son noktada NEE yapılıyor ama
/// tamamlayıcı BSDF ışını hiç izlenmiyordu, enerji kayboluyordu (bulgu lighting-m1).
Color3f PathTracer::Li(const Ray& ray, const Scene& scene, Sampler& sampler, PrimaryHit* primary) const {
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
    // Düzgün ışık seçimi: her ışığın seçilme olasılığı 1/lightCount.
    const float lightPmf = lightCount > 0 ? 1.0f / static_cast<float>(lightCount) : 0.0f;
    const int shadowSamples = m_shadowQuality;

    // Tek bir NaN/Inf katkı ilerleyen (progressive) pikseli kalıcı olarak bozar;
    // böyle bir katkıyı atlarız (bulgu lighting-10). Sonlu değerler aynen eklenir.
    auto accumulate = [&L](const Color3f& c) {
        if (c.isFinite()) L += c;
    };

    for (int depth = 0;; ++depth) {
        SurfaceInteraction isect;
        bool hit = scene.intersect(currentRay, isect);
        if (depth == 0 && primary) {
            primary->hit = hit;
            if (hit) primary->isect = isect;
        }

        if (!hit) {
            // Kameradan doğrudan görülen arka plan (yalnız birincil ışın; yansımalar ve
            // cam arkası her zaman ortamı görür). Şeffafta katkı 0, alfa da 0 olur.
            if (depth == 0 && primary && scene.background().mode != Background::Mode::Environment) {
                if (scene.background().mode == Background::Mode::Color) L = scene.background().color;
                break;
            }
            if (env) {
                Color3f Le = env->eval(currentRay.direction);
                float weight = 1.0f;
                if (!specularBounce) {
                    // BSDF örneği ortama kaçtı: NEE'nin aynı yönü bulma pdf'i ile MIS.
                    float lightPdf = lightPmf * env->pdfLi(currentRay.direction, lastNormal);
                    weight = powerHeuristic(1, lastBsdfPdf, shadowSamples, lightPdf);
                }
                accumulate(throughput * Le * weight);
            }
            break;
        }

        if (isect.material) {
            Color3f emitted = isect.material->emitted(isect);
            if (!emitted.isBlack()) {
                float weight = 1.0f;
                if (!specularBounce) {
                    // Diffuse/glossy hit of an emissive quad: complement of NEE's weight.
                    // Kayıtlı ışık değilse (lightPdf = 0) NEE onu bulamaz → ağırlık 1.
                    float lightPdf = lightPmf * areaLightSolidAnglePdf(lights, lastPoint, isect.point);
                    if (lightPdf > 0.0f && lastBsdfPdf > 0.0f) {
                        weight = powerHeuristic(1, lastBsdfPdf, shadowSamples, lightPdf);
                    }
                }
                accumulate(throughput * emitted * weight);
            }
        }

        if (!isect.material || depth >= m_maxDepth) {
            break;
        }

        // Approximate contact AO (multiplies remaining path; cheap studio polish).
        // m_aoStrength = 0 iken tamamen etkisizdir (örnek de tüketmez).
        if (m_aoStrength > 0.0f && depth == 0) {
            throughput *= approxAo(scene, isect, sampler, m_aoStrength, m_shadowQuality);
        }

        const Vec3f wo = -currentRay.direction;
        const bool isSpecular = isect.material->isDelta();

        if (lightCount > 0 && !isSpecular) {
            for (int s = 0; s < shadowSamples; ++s) {
                int lightIndex = std::min(static_cast<int>(sampler.get1D() * lightCount), lightCount - 1);
                LightSample ls;
                bool fromEnv = false;

                if (lightIndex < explicitLights) {
                    ls = lights[lightIndex]->sampleLi(isect, sampler.get2D());
                    if (!ls.isValid()) continue;
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
                    float bsdfPdf = 0.0f;
                    Color3f f = isect.material->evalPdf(wo, ls.wi, isect, bsdfPdf);
                    // |cos|: ışık yüzeyin arkasında olabilir; BSDF geçirgense (kaba cam,
                    // difüz geçirgenlik) f ≠ 0 döner ve bu ışık da sayılmalıdır. Saf yansıtıcı
                    // malzemede arka yarıküre için f = 0 olduğundan |cos| zararsızdır.
                    // (lighting-3 / materials-6: eskiden max(0, cos) ile atılıyordu, ama BSDF
                    // tarafındaki MIS yine de ağırlığı düşürüyordu → enerji kaybı.)
                    float cosTheta = std::abs(ls.wi.dot(isect.normal));

                    if (!f.isBlack() && cosTheta > 0.0f) {
                        float lightPdf = lightPmf * ls.pdf;
                        float weight = 1.0f;
                        bool delta = !fromEnv && lights[lightIndex]->isDelta();
                        if (!delta) {
                            weight = powerHeuristic(shadowSamples, lightPdf, 1, bsdfPdf);
                        }
                        // Tahminci: f·Li·|cos| / (n_l · pdf_l), MIS ağırlığıyla.
                        accumulate(throughput * f * ls.Li
                                   * (cosTheta * weight / (lightPdf * static_cast<float>(shadowSamples))));
                    }
                }
            }
        }

        // BSDF örneklemesi: uc lob seçimi (yansıma/kırılma), u yön için.
        float uc = sampler.get1D();
        Vec2f u = sampler.get2D();
        Vec3f wi;
        Color3f f;
        float pdf = 0.0f;
        if (!isect.material->sampleWithLobe(wo, isect, uc, u, wi, f, pdf)) {
            break;
        }

        if (pdf <= 0.0f || f.isBlack()) {
            break;
        }

        float cosTheta = std::abs(wi.dot(isect.normal));
        throughput *= f * (cosTheta / pdf);
        if (!throughput.isFinite()) break;
        lastBsdfPdf = pdf;
        lastNormal = isect.normal;
        lastPoint = isect.point;
        specularBounce = isect.material->isDelta();

        Vec3f nextRayOrigin = offsetRayOrigin(isect.point, isect.normal, wi);
        currentRay = Ray(nextRayOrigin, wi);

        // Rus ruleti (PBRT-v4): hayatta kalma olasılığı = throughput'un EN BÜYÜK bileşeni.
        // q = 1 - max; q > 0 ise ölçek 1/(1-q) ile tahminci yansız kalır. max ≥ 1 ise
        // yol hiç kesilmez (q ≤ 0) ve rastgele sayı da tüketilmez. Eskiden luminans
        // kullanılıyordu: doygun renkli (ör. saf mavi) yollar haksız yere öldürülüyordu.
        if (depth >= m_russianRouletteDepth) {
            float maxComponent = std::max({throughput.r, throughput.g, throughput.b});
            float q = std::max(0.0f, 1.0f - maxComponent);
            if (q > 0.0f) {
                if (sampler.get1D() < q) {
                    break;
                }
                throughput /= (1.0f - q);
            }
        }
    }

    return L;
}

} // namespace photon
