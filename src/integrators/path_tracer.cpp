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
#include "geometry/ray_spawn.h"
#include "core/sampling/sampling.h"
#include "core/sampling/phase.h"
#include "core/color/spectral.h"
#include <algorithm>
#include <cmath>

namespace photon {

namespace {

float approxAo(const Scene& scene, const SurfaceInteraction& isect, Sampler& sampler,
               float strength, int samples) {
    if (strength <= 0.0f || samples <= 0) return 1.0f;

    Frame frame(isect.normal);
    float occ = 0.0f;
    const float maxDist = 0.35f; // ponytail: short-range contact AO, not full GI
    for (int i = 0; i < samples; ++i) {
        Vec3f local = cosineSampleHemisphere(sampler.get2D());
        Vec3f dir = frame.toWorld(local).normalized();
        Ray aoRay(spawnOrigin(isect, dir), dir, 0.0f, maxDist);
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

/// NEE gölge ışını. Başlangıç alıcının yüzeyinden sağlam biçimde ofsetlenir (ray_spawn.h).
/// Işık örneği alıcıya göre ölçüldüğü için wi boyunca gidilirse ışığın düzlemi örnekten ÖNCE
/// kesilebilir ve alan ışığının kendi dörtgeni kendi örneğini örter (yakın ışıkta tam karanlık).
/// Bu yüzden sonlu uzaklıktaki ışıklarda ışın ofsetli başlangıçtan örnek noktasına yönelir ve
/// göreli bir payla önce durur. false → ışın kurulamadı (örnek alıcıya fazla yakın).
bool makeShadowRay(const SurfaceInteraction& isect, const LightSample& ls, bool /*pointOnSurface*/, Ray& out) {
    if (!(ls.distance < INFINITY_F) || ls.distance >= 1e29f) {
        out = Ray(shadowOrigin(isect, ls.wi), ls.wi, 0.0f, 1e30f);
        return true;
    }
    // ponytail: stop short of the sample; a contact occluder inside the slack is missed until shadow rays skip light prims
    return spawnRayTo(isect, isect.point + ls.wi * ls.distance, out);
}

/// Gölge ışınının geçirgenliği: ince duvarlı camlar (doğrultu değiştirmeyen yüzeyler)
/// renkli geçirgenlikle aşılır, başka her yüzey ışığı tamamen keser. Sahnede böyle bir
/// malzeme yoksa hızlı "herhangi bir isabet" testi kullanılır.
Color3f shadowTransmittance(const Scene& scene, const Ray& shadow) {
    if (!scene.hasShadowTransmitters()) return scene.intersectAny(shadow) ? Color3f::black() : Color3f(1.0f);
    Color3f T(1.0f);
    Vec3f origin = shadow.origin;
    const Vec3f dir = shadow.direction;
    float remaining = shadow.tMax;
    for (int i = 0; i < 16; ++i) {
        Ray r(origin, dir, 0.0f, remaining);
        SurfaceInteraction hit;
        if (!scene.intersect(r, hit)) return T;
        if (!hit.material || !hit.material->transmitsShadows()) return Color3f::black();
        T *= hit.material->shadowTransmittance(hit, dir);
        if (T.isBlack()) return T;
        // Zarın öbür yüzüne sağlam ofsetle geç (terminatör noktası değil: doğrultu korunur).
        origin = offsetRayOrigin(hit.point, hit.pError, hit.ng.lengthSquared() > 0.0f ? hit.ng : hit.normal, dir);
        remaining -= hit.t;
        if (remaining <= 0.0f) return T;
    }
    return Color3f::black();
}

/// Yuvarlatılmış kenar normali (Cycles "Bevel" düğümünün sadeleştirilmiş hali). Nokta
/// etrafında R yarıçaplı bir diskte örnek noktalar seçilir; her birinden üç eksenden biri
/// boyunca (normal ya da iki teğet) 2R uzunluğunda kısa ışın atılır. Aynı nesneye R'nin
/// içinde çarpan ışınların yüzey normalleri ortalanır: düz yüzde sonuç değişmez, kenarın
/// R yakınında komşu yüzün normali karışır ve keskin kenar yumuşak bir pah gibi parlar.
Vec3f roundedEdgeNormal(const Scene& scene, const SurfaceInteraction& isect, float radius, Sampler& sampler) {
    const Vec3f N = isect.normal;
    const Frame f(N);
    Vec3f sum = N * 0.25f; // hiç isabet yoksa (ince parça) orijinal normal kalır
    constexpr int kSamples = 6;
    for (int i = 0; i < kSamples; ++i) {
        const float ua = sampler.get1D();
        const Vec2f d = uniformSampleDisk(sampler.get2D()) * radius;
        Vec3f axis, s, t;
        if (ua < 0.5f) { axis = f.n; s = f.s; t = f.t; }
        else if (ua < 0.75f) { axis = f.s; s = f.t; t = f.n; }
        else { axis = f.t; s = f.n; t = f.s; }
        // Eksenin iki yönünü de dene: kenarın hangi tarafta olduğunu bilmiyoruz.
        for (float sign : {1.0f, -1.0f}) {
            const Vec3f origin = isect.point + s * d.x + t * d.y + axis * (sign * radius);
            Ray r(origin, axis * -sign, 0.0f, 2.0f * radius);
            SurfaceInteraction hit;
            if (!scene.intersect(r, hit) || hit.hitObject != isect.hitObject) continue;
            if ((hit.point - isect.point).lengthSquared() > 2.25f * radius * radius) continue;
            const Vec3f outward = hit.frontFace ? hit.normal : -hit.normal;
            sum += outward * std::abs(outward.dot(axis)); // eksene dik yüzleri az say (izdüşüm)
        }
    }
    // Ortalama dış normal; çarpılan yüzün ışına bakan tarafına çevrilir.
    Vec3f n = sum.normalized();
    const Vec3f outwardN = isect.frontFace ? N : -N;
    if (n.dot(outwardN) < 0.0f) n = -n;
    return isect.frontFace ? n : -n;
}

} // namespace

const char* lightPassName(LightPass pass) {
    switch (pass) {
        case LightPass::DiffuseDirect: return "DiffDir";
        case LightPass::DiffuseIndirect: return "DiffInd";
        case LightPass::GlossyDirect: return "GlossDir";
        case LightPass::GlossyIndirect: return "GlossInd";
        case LightPass::TransmissionDirect: return "TransDir";
        case LightPass::TransmissionIndirect: return "TransInd";
        case LightPass::Emission: return "Emit";
        case LightPass::Background: return "Env";
        case LightPass::Count: break;
    }
    return "Pass";
}

Vec3f PathTracer::roundedEdgeNormalForTest(const Scene& scene, const SurfaceInteraction& isect, float radius,
                                           Sampler& sampler) {
    return roundedEdgeNormal(scene, isect, radius, sampler);
}

bool PathTracer::randomWalkSubsurface(const Scene& scene, const SurfaceInteraction& entry, const Vec3f& dir,
                                      const SubsurfaceCoeffs& coeffs, Sampler& sampler, Color3f& throughput,
                                      SurfaceInteraction& exitSi) {
    const Color3f sigmaS = coeffs.albedo * coeffs.sigmaT;
    Vec3f d = dir.normalized();
    // Giriş: yüzeyin hemen içinden başla (normal ışığın geldiği tarafa bakar).
    Vec3f origin = spawnOrigin(entry, d);
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
Color3f PathTracer::Li(const Ray& ray, const Scene& scene, Sampler& sampler, PrimaryHit* primary,
                       LightPasses* passes) const {
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
    // Işık seçimi köşeye özeldir (lights/light_sampler.h: az ışıkta kapsamlı, çok ışıkta ışık
    // BVH'si); MIS'te bir önceki köşenin dağılımı lazım, bu yüzden bağlam yalnız NEE yapılan
    // köşede yeniden kurulur ve sonraki isabete kadar kalır.
    const LightSampler& lightSampler = scene.lightSampler();
    LightSampler::Context picker;
    const int shadowSamples = m_shadowQuality;
    bool cameraRay = true; // birincil ışın ya da yalnız delta kırılmalarla devamı

    // Tek bir NaN/Inf katkı ilerleyen (progressive) pikseli kalıcı olarak bozar;
    // böyle bir katkıyı atlarız (bulgu lighting-10). Sonlu değerler aynen eklenir.
    auto accumulate = [&L](const Color3f& c) -> Color3f {
        if (!c.isFinite()) return Color3f::black();
        L += c;
        return c;
    };
    // Delta olmayan (difüz/parlak/yüzey altı) saçılma sayısı: kırpma "doğrudan"ı (ilk böyle
    // köşeden görülen ışık) korur; cam ve ayna zinciri doğrudanlığı bozmaz.
    int diffuseBounces = 0;
    int passThroughs = 0;
    // Dolaylı katkı: ışığa ikinci ya da sonraki köşeden ulaşan yol (isteğe bağlı kırpma).
    auto accumulateIndirect = [&](const Color3f& c) -> Color3f {
        if (m_clampIndirect > 0.0f) {
            const float m = std::max({c.r, c.g, c.b});
            if (m > m_clampIndirect) return accumulate(c * (m_clampIndirect / m));
        }
        return accumulate(c);
    };

    // ── Render geçişleri ──
    // İlk yüzeyde örneklenen yönün lob payları (difüz, parlak, geçirgen; kanal başına, toplam 1).
    Color3f firstLobe[3] = {Color3f(1.0f), Color3f(0.0f), Color3f(0.0f)};
    auto passAt = [](int lobe, bool direct) {
        return static_cast<int>(lobe == 0 ? (direct ? LightPass::DiffuseDirect : LightPass::DiffuseIndirect)
                                : lobe == 1 ? (direct ? LightPass::GlossyDirect : LightPass::GlossyIndirect)
                                            : (direct ? LightPass::TransmissionDirect : LightPass::TransmissionIndirect));
    };
    auto credit = [&](LightPass p, const Color3f& added) {
        if (passes) passes->c[static_cast<int>(p)] += added;
    };
    // İlk yüzeyden sonra bulunan ışık: ilk yüzeyin lob paylarıyla; ilk sekmede doğrudan.
    auto creditPath = [&](const Color3f& added, bool direct) {
        if (!passes) return;
        for (int k = 0; k < 3; ++k) passes->c[passAt(k, direct)] += added * firstLobe[k];
    };

    for (;;) {
        bool hit = true;
        Vec3f wo;
        const bool pendingExitVertex = pendingExit;
        if (pendingExit) {
            pendingExit = false;
            wo = isect.normal; // Lambert çıkışı: wo yalnız yarıküre testi için
        } else {
            isect = SurfaceInteraction{};
            const Vec3f segmentStart = currentRay.origin;
            hit = scene.intersect(currentRay, isect);
            // Kameraya görünmez ışık dörtgenleri: kamera ışını içinden geçip devam eder.
            for (int skip = 0; hit && cameraRay && skip < 8 && scene.cameraInvisible(isect); ++skip) {
                currentRay = Ray(offsetRayOrigin(isect.point, isect.pError, isect.ng, currentRay.direction),
                                 currentRay.direction, 0.0f, 1e30f);
                isect = SurfaceInteraction{};
                hit = scene.intersect(currentRay, isect);
            }
            if (firstSegment && primary) {
                primary->hit = hit;
                if (hit) primary->isect = isect;
            }
            firstSegment = false;

            // Işın bir cismin içinde ilerliyorsa: soğurma ve (varsa) saçılma.
            if (const Medium* med = media.current()) {
                const float dirLen = currentRay.direction.length();
                const float segment = hit ? (isect.point - segmentStart).length() : INFINITY_F;
                if (med->scatters()) {
                    float t = 0.0f;
                    if (sampleFreeFlight(med->sigmaT(), med->sigmaS, segment, hit, throughput, sampler, throughput, t)) {
                        if (++mediumEvents > kMaxMediumEvents || !throughput.isFinite()) break;
                        const Vec3f d = currentRay.direction / dirLen;
                        currentRay = Ray(segmentStart + d * t, sampleHenyeyGreenstein(d, med->g, sampler.get2D()));
                        cameraRay = false;
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
                // Kameradan doğrudan görülen arka plan (birincil ışın ya da ince duvardan düz geçen
                // devamı; yansımalar ve kırıcı cam arkası her zaman ortamı görür). İnce camın tonu
                // throughput'ta olduğu için arka plan onunla çarpılır. Şeffafta renk katkısı 0;
                // kapsama, camın geçirmediği orandır (yalnız düz geçiş varsa ≠ 1).
                if (depth == 0 && primary && scene.background().mode != Background::Mode::Environment) {
                    if (scene.background().mode == Background::Mode::Color)
                        credit(LightPass::Background, accumulate(throughput * scene.background().color));
                    else if (passThroughs > 0) primary->coverage = std::max(0.0f, 1.0f - throughput.luminance());
                    break;
                }
                if (env) {
                    Color3f Le = env->eval(currentRay.direction);
                    float weight = 1.0f;
                    if (!specularBounce) {
                        // BSDF örneği ortama kaçtı: NEE'nin aynı yönü bulma pdf'i ile MIS.
                        float lightPdf = picker.pmf(explicitLights) * env->pdfLi(currentRay.direction, lastNormal);
                        weight = powerHeuristic(1, lastBsdfPdf, shadowSamples, lightPdf);
                    }
                    const Color3f added = diffuseBounces >= 2 ? accumulateIndirect(throughput * Le * weight)
                                                              : accumulate(throughput * Le * weight);
                    if (depth == 0) credit(LightPass::Background, added);
                    else creditPath(added, depth == 1);
                }
                break;
            }

            // Gölge yakalayıcı: kamera zemin yerine arka planı görür, üzerine zemine düşen
            // gölgenin oranı biner. Oran = örneklenen doğrudan ışığın görünen payı (Cycles'ın
            // "shadow catcher"ı gibi oran tahmini). Dolaylı ışınlar zemini normal yüzey görür:
            // nesneler zeminden sekme ışığı alır ve onu yansıtır.
            if (cameraRay && scene.isShadowCatcher(isect)) {
                const Background& bgd = scene.background();
                Color3f bg;
                if (bgd.mode == Background::Mode::Color) bg = bgd.color;
                else if (bgd.mode == Background::Mode::Environment && env) bg = env->eval(currentRay.direction);
                float visible = 0.0f, total = 0.0f;
                if (lightCount > 0) {
                    picker = lightSampler.prepare(isect.point, isect.normal);
                    const int n = 4 * shadowSamples;
                    for (int k = 0; k < n; ++k) {
                        float pmf = 0.0f;
                        const int li = picker.sample(sampler.get1D(), pmf);
                        if (li < 0 || !(pmf > 0.0f)) {
                            sampler.get2D();
                            continue;
                        }
                        const LightSample ls = li < explicitLights ? lights[static_cast<size_t>(li)]->sampleLi(isect, sampler.get2D())
                                                                   : env->sampleLi(isect, sampler.get2D());
                        const float cosT = ls.wi.dot(isect.normal);
                        if (!ls.isValid() || cosT <= 0.0f) continue;
                        const float c = ls.Li.luminance() * cosT / (pmf * ls.pdf);
                        if (!(c > 0.0f) || !std::isfinite(c)) continue;
                        total += c;
                        const bool onSurface = li < explicitLights && !lights[static_cast<size_t>(li)]->isDelta();
                        Ray shadow;
                        if (makeShadowRay(isect, ls, onSurface, shadow))
                            visible += c * shadowTransmittance(scene, shadow).luminance();
                    }
                }
                const float ratio = total > 0.0f ? std::clamp(visible / total, 0.0f, 1.0f) : 1.0f;
                if (primary && depth == 0) {
                    primary->catcherAlpha = 1.0f - ratio;
                    primary->catcherBackground = bg;
                }
                credit(LightPass::Background, accumulate(throughput * bg * ratio));
                break;
            }

            if (isect.material) {
                Color3f emitted = isect.material->emitted(isect);
                if (!emitted.isBlack()) {
                    float weight = 1.0f;
                    if (!specularBounce) {
                        // Diffuse/glossy hit of an emissive quad: complement of NEE's weight.
                        // Kayıtlı ışık değilse (lightPdf = 0) NEE onu bulamaz → ağırlık 1.
                        const int li = scene.emitterIndex(isect);
                        float lightPdf = li >= 0 ? picker.pmf(li) * lights[static_cast<size_t>(li)]->pdfLiHit(lastPoint, isect)
                                                 : 0.0f;
                        if (lightPdf > 0.0f && lastBsdfPdf > 0.0f) {
                            weight = powerHeuristic(1, lastBsdfPdf, shadowSamples, lightPdf);
                        }
                    }
                    const Color3f added = diffuseBounces >= 2 ? accumulateIndirect(throughput * emitted * weight)
                                                              : accumulate(throughput * emitted * weight);
                    if (depth == 0) credit(LightPass::Emission, added);
                    else creditPath(added, depth == 1);
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

        // Yuvarlatılmış kenarlar: gölgelendirme normali komşu yüzeylerle harmanlanır (ilk üç
        // köşede; daha derin sekmelerde görünmez, maliyete değmez). wo'yu arkada bırakan bir
        // eğim siyah kenar yapacağından o durumda orijinal normal korunur.
        if (depth <= 2 && isect.material->roundEdgeRadius() > 0.0f && !pendingExitVertex) {
            const Vec3f n = roundedEdgeNormal(scene, isect, isect.material->roundEdgeRadius(), sampler);
            if (n.dot(wo) > 0.02f) isect.normal = n;
        }

        // Approximate contact AO (multiplies remaining path; cheap studio polish).
        // m_aoStrength = 0 iken tamamen etkisizdir (örnek de tüketmez).
        if (m_aoStrength > 0.0f && depth == 0) {
            throughput *= approxAo(scene, isect, sampler, m_aoStrength, m_shadowQuality);
        }

        const bool isSpecular = isect.material->isDelta();
        if (lightCount > 0 && !isSpecular) {
            picker = lightSampler.prepare(isect.point, isect.normal);
            for (int s = 0; s < shadowSamples; ++s) {
                float lightPmf = 0.0f;
                const int lightIndex = picker.sample(sampler.get1D(), lightPmf);
                if (lightIndex < 0 || !(lightPmf > 0.0f)) {
                    sampler.get2D(); // boyut düzeni sabit kalsın
                    continue;
                }
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

                Ray shadowRay;
                if (!makeShadowRay(isect, ls, !fromEnv && !lights[lightIndex]->isDelta(), shadowRay)) continue;
                const Color3f vis = shadowTransmittance(scene, shadowRay);

                if (!vis.isBlack()) {
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
                        const Color3f c = throughput * f * ls.Li * vis
                                          * (cosTheta * weight / (lightPdf * static_cast<float>(shadowSamples)));
                        const Color3f added = diffuseBounces >= 1 ? accumulateIndirect(c) : accumulate(c);
                        if (passes && !added.isBlack()) {
                            if (depth == 0) {
                                // İlk yüzey: bu ışık örneğinin değeri lob gruplarına bölünür.
                                const LobeSplit sp = isect.material->evalSplit(wo, ls.wi, isect);
                                const Color3f* parts[3] = {&sp.diffuse, &sp.glossy, &sp.transmission};
                                const Color3f sum = sp.diffuse + sp.glossy + sp.transmission;
                                for (int k = 0; k < 3; ++k) {
                                    Color3f frac;
                                    for (int ch = 0; ch < 3; ++ch) frac[ch] = sum[ch] > 0.0f ? (*parts[k])[ch] / sum[ch] : (k == 0 ? 1.0f : 0.0f);
                                    passes->c[passAt(k, true)] += added * frac;
                                }
                            } else {
                                creditPath(added, false);
                            }
                        }
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
            throughput *= bs.weight; // yüzey altı ışığı "doğrudan" sayılır: kırpılmaz
            SurfaceInteraction exitSi;
            if (!randomWalkSubsurface(scene, isect, bs.wi, coeffs, sampler, throughput, exitSi)) break;
            // Çıkış köşesi: normal dışa, malzeme beyaz Lambert. Derinlik artmaz.
            exitSi.normal = -exitSi.normal;
            exitSi.material = &subsurfaceExitMaterial();
            isect = exitSi;
            pendingExit = true;
            cameraRay = false;
            continue;
        }

        if (bs.pdf <= 0.0f || bs.f.isBlack()) {
            break;
        }

        if (bs.passThrough) {
            // İnce duvardan düz geçiş: yol bir önceki köşenin MIS durumunu korur (lastPoint,
            // lastBsdfPdf, specularBounce değişmez). Aynı ışık gölge ışınıyla da (geçirgenlikle)
            // bulunduğu için iki strateji böylece tutarlı tartılır.
            throughput *= bs.f * (std::abs(bs.wi.dot(isect.normal)) / bs.pdf);
            if (!throughput.isFinite()) break;
            currentRay = spawnRay(isect, bs.wi);
            if (++passThroughs > 64) break; // sonsuz cam yığınına karşı sigorta (sekme sayılmaz)
            continue;
        }

        const Vec3f wi = bs.wi;
        if (passes && depth == 0) {
            // İlk yüzeyin lob payları: bu yönden gelecek tüm ışık bunlarla geçişlere dağılır.
            if (bs.delta) {
                const bool refracted = wo.dot(isect.ng) * wi.dot(isect.ng) < 0.0f;
                firstLobe[0] = Color3f(0.0f);
                firstLobe[1] = refracted ? Color3f(0.0f) : Color3f(1.0f);
                firstLobe[2] = refracted ? Color3f(1.0f) : Color3f(0.0f);
            } else {
                const LobeSplit sp = isect.material->evalSplit(wo, wi, isect);
                const Color3f sum = sp.diffuse + sp.glossy + sp.transmission;
                for (int ch = 0; ch < 3; ++ch) {
                    const bool ok = sum[ch] > 0.0f;
                    firstLobe[0][ch] = ok ? sp.diffuse[ch] / sum[ch] : 1.0f;
                    firstLobe[1][ch] = ok ? sp.glossy[ch] / sum[ch] : 0.0f;
                    firstLobe[2][ch] = ok ? sp.transmission[ch] / sum[ch] : 0.0f;
                }
            }
        }
        float cosTheta = std::abs(wi.dot(isect.normal));
        throughput *= bs.f * (cosTheta / bs.pdf);
        if (!throughput.isFinite()) break;
        lastBsdfPdf = bs.pdf;
        lastNormal = isect.normal;
        lastPoint = isect.point;
        specularBounce = bs.delta;
        if (!bs.delta) ++diffuseBounces;
        cameraRay = cameraRay && bs.delta && wi.dot(isect.normal) < 0.0f; // yalnız delta kırılma

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

        currentRay = spawnRay(isect, wi);
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
