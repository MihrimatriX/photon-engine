// Çok sekmeli yol izleyici (path tracer): her yüzey noktasında ışık örneklemesi (NEE)
// ve BSDF örneklemesi yapılır, ikisi Veach'in güç sezgiseli (MIS) ile birleştirilir.
// Rus ruleti, NaN/Inf koruması ve isteğe bağlı kontak AO da burada.

#include "integrators/path_tracer.h"
#include "materials/material.h"
#include "materials/lambertian.h"
#include "lights/light.h"
#include "lights/area_light.h"
#include "lights/environment_light.h"
#include "samplers/sampler.h"
#include "engine/scene.h"
#include "core/math/frame.h"
#include "core/math/utils.h"
#include "core/sampling/sampling.h"
#include "core/sampling/phase.h"
#include "core/color/spectral.h"
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

namespace {

/// Yoğun ortamda (süt, mum) tek yolun saçılma olayı sınırı: sonsuz döngü sigortası.
constexpr int kMaxMediumEvents = 1024;
constexpr int kMaxWalkSteps = 256;

/// Rastgele yürüyüşün çıkış köşesi: Cycles gibi yürüyüşten çıkan ışık, çıkış noktasında
/// beyaz bir Lambert yüzeyiyle (normal DIŞA) saçılır; renk zaten yürüyüşte uygulandı.
const Lambertian& subsurfaceExitMaterial() {
    static const Lambertian m(Color3f(1.0f));
    return m;
}

/// Kırılan yolun hangi cismin içinde olduğunu izleyen küçük yığın. İç içe hacimler
/// (camın içinde sıvı) için en üstteki geçerlidir. Taşma sessizce yok sayılır.
struct MediumStack {
    struct Entry {
        const Material* owner;
        const Medium* medium;
    };
    Entry e[4]{};
    int n = 0;

    const Medium* current() const { return n > 0 ? e[n - 1].medium : nullptr; }
    void enter(const Material* m) {
        if (n < 4) e[n++] = {m, m->interiorMedium()};
    }
    void leave(const Material* m) {
        for (int i = n - 1; i >= 0; --i) {
            if (e[i].owner != m) continue;
            for (int j = i; j < n - 1; ++j) e[j] = e[j + 1];
            --n;
            return;
        }
    }
};

Color3f expNeg(const Color3f& sigma, float t) {
    return Color3f(std::exp(-sigma.r * t), std::exp(-sigma.g * t), std::exp(-sigma.b * t));
}

/// Homojen ortamda serbest uçuş örneklemesi, spektral MIS ile (PBRT-v4 §14.2, Cycles).
///
/// σt kanaldan kanala değişir (renkli ortam). Bir kanal c, @p channelWeights'e orantılı
/// olasılıkla seçilir; mesafe o kanalın üstel dağılımından çekilir: t = −ln(1−u)/σt[c].
/// Birleşik pdf tüm kanalların karışımıdır, böylece tahminci yansızdır:
///   saçılma (t < tMax):  pdf = Σ p_c σt_c e^{−σt_c t},  ağırlık = σs · e^{−σt t} / pdf
///   yüzeye ulaşma:       P   = Σ p_c e^{−σt_c tMax},    ağırlık = e^{−σt tMax} / P
/// @return true → t = @p tScatter'da saçılma oldu; false → yüzeye (ya da sonsuza) ulaşıldı.
bool sampleFreeFlight(const Color3f& sigmaT, const Color3f& sigmaS, float tMax, bool hitSurface,
                      Color3f channelWeights, Sampler& sampler, Color3f& throughput, float& tScatter) {
    float wsum = channelWeights.r + channelWeights.g + channelWeights.b;
    if (!(wsum > 0.0f)) {
        channelWeights = Color3f(1.0f);
        wsum = 3.0f;
    }
    const Color3f pc = channelWeights / wsum;
    const float uc = sampler.get1D();
    const int c = uc < pc.r ? 0 : (uc < pc.r + pc.g ? 1 : 2);
    const float u = sampler.get1D();
    const float st = sigmaT[c];
    const float t = st > 0.0f ? -std::log(std::max(1.0f - u, 1e-12f)) / st : INFINITY_F;

    if (t < tMax) {
        const Color3f Tr = expNeg(sigmaT, t);
        const float pdf = pc.r * sigmaT.r * Tr.r + pc.g * sigmaT.g * Tr.g + pc.b * sigmaT.b * Tr.b;
        if (!(pdf > 0.0f)) {
            throughput = Color3f::black();
            return false;
        }
        throughput *= sigmaS * Tr / pdf;
        tScatter = t;
        return true;
    }
    // Yüzeye ulaşıldı. Iskalayan ışında (tMax = ∞) σt > 0 olan kanallar tamamen söner.
    Color3f Tr;
    for (int k = 0; k < 3; ++k) Tr[k] = hitSurface ? std::exp(-sigmaT[k] * tMax) : (sigmaT[k] > 0.0f ? 0.0f : 1.0f);
    const float P = pc.r * Tr.r + pc.g * Tr.g + pc.b * Tr.b;
    throughput = P > 0.0f ? throughput * Tr / P : Color3f::black();
    return false;
}

/// Rus ruleti (en büyük bileşene göre). false → yol öldü.
bool russianRoulette(Color3f& throughput, Sampler& sampler) {
    const float m = std::max({throughput.r, throughput.g, throughput.b});
    if (m >= 1.0f) return true;
    if (!(m > 0.0f) || sampler.get1D() >= m) return false;
    throughput /= m;
    return true;
}

} // namespace

bool PathTracer::randomWalkSubsurface(const Scene& scene, const SurfaceInteraction& entry, const Vec3f& dir,
                                      const SubsurfaceCoeffs& coeffs, Sampler& sampler, Color3f& throughput,
                                      SurfaceInteraction& exitSi) {
    const Color3f sigmaS = coeffs.albedo * coeffs.sigmaT;
    Vec3f d = dir.normalized();
    // Giriş: yüzeyin hemen içinden başla (normal ışığın geldiği tarafa bakar).
    Vec3f origin = offsetRayOrigin(entry.point, entry.normal, d);
    for (int step = 0; step < kMaxWalkSteps; ++step) {
        Ray r(origin, d);
        SurfaceInteraction hit;
        const bool h = scene.intersect(r, hit);
        const float tMax = h ? hit.t : INFINITY_F;
        float t = 0.0f;
        // Kanal seçimi throughput · albedo ile (Cycles): sönmekte olan kanalı seçmek boşa iş.
        const bool scattered =
            sampleFreeFlight(coeffs.sigmaT, sigmaS, tMax, h, throughput * coeffs.albedo, sampler, throughput, t);
        if (!throughput.isFinite() || throughput.isBlack()) return false;
        if (!scattered) {
            if (!h) return false; // açık geometriden kaçtı
            exitSi = hit;
            return true;
        }
        origin = r.at(t);
        d = sampleHenyeyGreenstein(d, coeffs.g, sampler.get2D());
        if (step >= 8 && !russianRoulette(throughput, sampler)) return false;
    }
    return false;
}



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
///
/// Hacimler: kırılan yol bir cismin içine girince (cam, sıvı) o cismin ortamı yığına
/// itilir; içerideki her doğru parçasında soğurma (Beer–Lambert) ve gerekirse saçılma
/// örneklenir. İçeride NEE yapılmaz: ışıklar kırıcı sınırın dışında, gölge ışını sınıra
/// takılır; bu yüzden saçılmadan sonra bulunan ışık yayımı tam ağırlık alır.
///
/// Yüzey altı (SSS): malzeme "subsurface" lobunu seçerse rastgele yürüyüş yapılır ve yol
/// çıkış noktasında beyaz Lambert ile devam eder (NEE dahil). Yürüyüş sekme sayılmaz.
///
/// Dispersiyon: yol dalga boyuna bağlı bir cama ilk çarptığında tek bir λ seçilir;
/// throughput λ'nın RGB ağırlığıyla çarpılır ve yolun geri kalanı bu λ ile izlenir.
Color3f PathTracer::Li(const Ray& ray, const Scene& scene, Sampler& sampler, PrimaryHit* primary) const {
    Color3f L = Color3f::black();
    Color3f throughput = Color3f::white();
    Ray currentRay = ray;
    bool specularBounce = true;
    float lastBsdfPdf = 0.0f;
    Vec3f lastNormal(0, 1, 0);
    Vec3f lastPoint = ray.origin;
    MediumStack media;
    float lambda = 0.0f;       // 0 = RGB; > 0 = dispersiyon nedeniyle tek dalga boylu yol
    int mediumEvents = 0;
    bool firstSegment = true;
    bool pendingExit = false;  // SSS çıkış köşesi: isect yürüyüş tarafından dolduruldu
    SurfaceInteraction isect;
    int depth = 0;

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

    for (;;) {
        bool hit = true;
        Vec3f wo;
        if (pendingExit) {
            pendingExit = false;
            wo = isect.normal; // Lambert çıkışı: wo yalnız yarıküre testi için
        } else {
            isect = SurfaceInteraction{};
            hit = scene.intersect(currentRay, isect);
            if (firstSegment && primary) {
                primary->hit = hit;
                if (hit) primary->isect = isect;
            }
            firstSegment = false;

            // Işın bir cismin içinde ilerliyorsa: soğurma ve (varsa) saçılma.
            if (const Medium* med = media.current()) {
                const float dirLen = currentRay.direction.length();
                const float segment = hit ? isect.t * dirLen : INFINITY_F;
                if (med->scatters()) {
                    float t = 0.0f;
                    if (sampleFreeFlight(med->sigmaT(), med->sigmaS, segment, hit, throughput, sampler, throughput, t)) {
                        if (++mediumEvents > kMaxMediumEvents || !throughput.isFinite()) break;
                        const Vec3f d = currentRay.direction / dirLen;
                        currentRay = Ray(currentRay.origin + d * t, sampleHenyeyGreenstein(d, med->g, sampler.get2D()));
                        specularBounce = true; // içeride NEE yok → bulunan yayım tam ağırlık
                        if (mediumEvents > 8 && !russianRoulette(throughput, sampler)) break;
                        continue;
                    }
                } else if (hit) {
                    throughput *= expNeg(med->sigmaA, segment);
                } else if (!med->sigmaA.isBlack()) {
                    break; // açık mesh: sonsuz soğurucu yol
                }
                if (throughput.isBlack() || !throughput.isFinite()) break;
            }
            wo = -currentRay.direction;

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
        }

        if (!isect.material || depth >= m_maxDepth) {
            break;
        }

        // Dispersiyon: ilk dalga boyuna bağlı yüzeyde λ seçilir, yol tek renkli olur.
        if (lambda == 0.0f && isect.material->isDispersive()) {
            Color3f w;
            lambda = sampleWavelength(sampler.get1D(), w);
            throughput *= w;
        }
        isect.wavelength = lambda;

        // Approximate contact AO (multiplies remaining path; cheap studio polish).
        // m_aoStrength = 0 iken tamamen etkisizdir (örnek de tüketmez).
        if (m_aoStrength > 0.0f && depth == 0) {
            throughput *= approxAo(scene, isect, sampler, m_aoStrength, m_shadowQuality);
        }

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

        // BSDF örneklemesi: uc lob seçimi (yansıma/kırılma/yüzey altı), u yön için.
        BSDFSample bs;
        const float uc = sampler.get1D();
        const Vec2f u = sampler.get2D();
        if (!isect.material->sampleFull(wo, isect, uc, u, bs)) {
            break;
        }

        if (bs.subsurface) {
            SubsurfaceCoeffs coeffs;
            if (!isect.material->subsurfaceCoeffs(isect, coeffs)) break;
            throughput *= bs.weight;
            SurfaceInteraction exitSi;
            if (!randomWalkSubsurface(scene, isect, bs.wi, coeffs, sampler, throughput, exitSi)) break;
            // Çıkış köşesi: normal dışa, malzeme beyaz Lambert. Derinlik artmaz.
            exitSi.normal = -exitSi.normal;
            exitSi.material = &subsurfaceExitMaterial();
            isect = exitSi;
            pendingExit = true;
            continue;
        }

        if (bs.pdf <= 0.0f || bs.f.isBlack()) {
            break;
        }

        const Vec3f wi = bs.wi;
        float cosTheta = std::abs(wi.dot(isect.normal));
        throughput *= bs.f * (cosTheta / bs.pdf);
        if (!throughput.isFinite()) break;
        lastBsdfPdf = bs.pdf;
        lastNormal = isect.normal;
        lastPoint = isect.point;
        specularBounce = bs.delta;

        // Hacim sınırından geçiş: wo ve wi geometrik normalin farklı taraflarındaysa ışın
        // kırıldı. Dışarıdan geldiyse cismin ortamına gir, içeriden geldiyse çık.
        if (isect.material->isVolumeBoundary()) {
            const float a = wo.dot(isect.ng);
            const float b = wi.dot(isect.ng);
            if (a * b < 0.0f) {
                if (a > 0.0f) media.enter(isect.material);
                else media.leave(isect.material);
            }
        }

        Vec3f nextRayOrigin = offsetRayOrigin(isect.point, isect.normal, wi);
        currentRay = Ray(nextRayOrigin, wi);
        ++depth;

        // Rus ruleti (PBRT-v4): hayatta kalma olasılığı = throughput'un EN BÜYÜK bileşeni.
        // q = 1 - max; q > 0 ise ölçek 1/(1-q) ile tahminci yansız kalır. max ≥ 1 ise
        // yol hiç kesilmez (q ≤ 0) ve rastgele sayı da tüketilmez. Eskiden luminans
        // kullanılıyordu: doygun renkli (ör. saf mavi) yollar haksız yere öldürülüyordu.
        if (depth > m_russianRouletteDepth && !russianRoulette(throughput, sampler)) {
            break;
        }
    }

    return L;
}

} // namespace photon
