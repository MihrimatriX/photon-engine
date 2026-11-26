// renderer.cpp — Piksel örnekleme, karo dağıtımı, iptal ve gürültü giderme akışı.
#include "engine/renderer.h"
#include "engine/denoiser.h"
#include "core/threading/parallel.h"
#include "samplers/sobol_sampler.h"
#include "integrators/path_tracer.h"
#include "integrators/path_guiding.h"
#include "geometry/mesh.h"
#include "geometry/mesh_instance.h"
#include "geometry/sphere.h"
#include "geometry/triangle.h"
#include "materials/disney.h"
#include "materials/dielectric.h"
#include "materials/lambertian.h"
#include "materials/mirror.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

namespace photon {

namespace {

ThreadPool* makePoolOverride(const RenderSettings& settings, std::unique_ptr<ThreadPool>& owned) {
    if (settings.numThreads <= 0) return nullptr;
    owned = std::make_unique<ThreadPool>(static_cast<size_t>(settings.numThreads));
    return owned.get();
}

// Raster (x, y)'de y = 0 ÜSTTE; kameranın ekran uzayında v = 0 ALTTA.
Vec2f rasterToScreen(float x, float y, int width, int height) {
    return Vec2f(x / static_cast<float>(width), 1.0f - y / static_cast<float>(height));
}

Color3f clamp01(const Color3f& c) {
    return Color3f(std::clamp(c.r, 0.0f, 1.0f), std::clamp(c.g, 0.0f, 1.0f), std::clamp(c.b, 0.0f, 1.0f));
}

// OIDN'in "albedo" kanalı: yüzeyin dokusuz/ışıksız taban rengi. Ayna ve cam
// gibi delta yüzeylerde yansıtma rengi verilir.
Color3f primaryAlbedo(const SurfaceInteraction& si) {
    if (!si.material) return Color3f::black();
    if (const auto* disney = dynamic_cast<const DisneyMaterial*>(si.material))
        return disney->resolve(si).baseColor;
    if (const auto* lambert = dynamic_cast<const Lambertian*>(si.material))
        return lambert->albedo();
    if (const auto* glass = dynamic_cast<const Dielectric*>(si.material))
        return glass->tint();
    if (const auto* mirror = dynamic_cast<const Mirror*>(si.material))
        return mirror->reflectance();
    return Color3f(0.5f);
}

struct CryptoLookup {
    std::unordered_map<const void*, float> objects;
    std::unordered_map<const Material*, float> materials;
};

CryptoLookup buildCryptoLookup(const Scene& scene, RenderPassFilms& passes) {
    CryptoLookup lk;
    const auto& shapeNames = scene.shapeNames();
    const auto& shapeOwners = scene.shapeOwners();
    const auto& matNames = scene.materialNames();

    size_t anonShape = 0;
    size_t anonMat = 0;
    auto registerMat = [&](const Material* m) {
        if (!m || lk.materials.count(m)) return;
        std::string name;
        if (auto it = matNames.find(m); it != matNames.end() && !it->second.empty()) {
            name = it->second;
        } else {
            name = "material_" + std::to_string(anonMat++);
        }
        lk.materials[m] = passes.cryptoMaterial.registerName(name);
    };

    for (const auto& [m, name] : matNames) {
        if (m && !name.empty()) lk.materials[m] = passes.cryptoMaterial.registerName(name);
    }

    for (const auto& shape : scene.shapes()) {
        if (!shape) continue;
        const void* ptr = shape.get();
        std::string objName;
        if (auto it = shapeNames.find(ptr); it != shapeNames.end() && !it->second.empty()) {
            objName = it->second;
        } else if (auto oit = shapeOwners.find(ptr); oit != shapeOwners.end() && oit->second != 0) {
            objName = "object_" + std::to_string(oit->second);
        } else {
            objName = "shape_" + std::to_string(anonShape++);
        }
        lk.objects[ptr] = passes.cryptoObject.registerName(objName);

        if (const auto* inst = dynamic_cast<const MeshInstance*>(shape.get())) {
            registerMat(inst->material());
            if (inst->mesh()) registerMat(inst->mesh()->material());
        } else if (const auto* mesh = dynamic_cast<const TriangleMesh*>(shape.get())) {
            registerMat(mesh->material());
        } else if (const auto* sph = dynamic_cast<const Sphere*>(shape.get())) {
            registerMat(sph->material());
        } else if (const auto* tri = dynamic_cast<const Triangle*>(shape.get())) {
            registerMat(tri->material());
        }
    }

    for (const auto& [ptr, name] : shapeNames) {
        if (ptr && !name.empty() && !lk.objects.count(ptr)) {
            lk.objects[ptr] = passes.cryptoObject.registerName(name);
        }
    }
    return lk;
}

struct PixelSample {
    Color3f L;
    Color3f albedo;
    Color3f normal;
    float alpha = 1.0f;
    // Render geçişleri (yalnız istendiğinde dolar)
    LightPasses light;
    float depth = 0.0f;
    Vec3f position;
    Color3f objectId;
    Color3f materialId;
    float cryptoObjHash = 0.0f;
    float cryptoMatHash = 0.0f;
};

// Tek bir alt-piksel örneği. AOV istendiğinde ya da arka plan modu ortam değilse
// path tracer ilk kesişimi bize bildirir (ayrı bir birincil ışın atılmaz): ıskalayan
// kamera ışınına arka plan rengi/şeffaflık uygulanır, çarpanda albedo ve normal alınır.
PixelSample tracePixel(const Scene& scene, const Camera& camera, PathTracer& integrator,
                       const PixelFilterSampler& filter, Sampler& sampler, int x, int y, int sampleIndex,
                       int width, int height, bool wantAovs, bool wantPasses = false, bool motionBlur = false,
                       const CryptoLookup* crypto = nullptr) {
    sampler.startPixel(x, y);
    sampler.startSample(sampleIndex);

    // Alt-piksel konumu filtrenin dağılımından (filtre önem örneklemesi): örnek pikselin
    // merkezinden filtre yarıçapı kadar uzağa düşebilir, ağırlığı yine 1'dir.
    const Vec2f offset = filter.sample(sampler.get2D());
    Vec2f screen = rasterToScreen(static_cast<float>(x) + 0.5f + offset.x, static_cast<float>(y) + 0.5f + offset.y,
                                  width, height);
    Vec2f lensSample = sampler.get2D();
    Ray ray = motionBlur ? camera.generateRay(screen.x, screen.y, lensSample, sampler.get1D())
                         : camera.generateRay(screen.x, screen.y, lensSample);
    ray.coneWidth = camera.pixelConeWidth(height);
    ray.coneSpread = camera.pixelConeSpread(height);

    PixelSample out;
    const Background& bg = scene.background();
    if (!wantAovs && !wantPasses && bg.mode == Background::Mode::Environment) {
        out.L = integrator.Li(ray, scene, sampler);
        return out;
    }
    PathTracer::PrimaryHit primary;
    out.L = integrator.Li(ray, scene, sampler, &primary, wantPasses ? &out.light : nullptr);
    if (wantPasses && primary.hit) {
        out.depth = (primary.isect.point - ray.origin).length();
        out.position = primary.isect.point;
        out.objectId = idColor(scene.nodeUidOf(primary.isect));
        out.materialId = idColor(reinterpret_cast<uintptr_t>(primary.isect.material));
        if (crypto) {
            if (auto it = crypto->objects.find(primary.isect.hitObject); it != crypto->objects.end())
                out.cryptoObjHash = it->second;
            if (auto it = crypto->materials.find(primary.isect.material); it != crypto->materials.end())
                out.cryptoMatHash = it->second;
        }
    }
    if (primary.catcherAlpha >= 0.0f) {
        // Gölge yakalayıcı: kapsama = gölgenin opaklığı; albedo = görünen arka plan.
        out.albedo = clamp01(primary.catcherBackground);
        const Vec3f n = primary.isect.normal;
        out.normal = Color3f(n.x, n.y, n.z);
        out.alpha = bg.mode == Background::Mode::Transparent ? primary.catcherAlpha : 1.0f;
    } else if (primary.hit) {
        out.albedo = clamp01(primaryAlbedo(primary.isect));
        if (primary.coverage >= 0.0f) {
            // İnce camın arkasındaki şeffaf fon: kısmi kapsama.
            out.alpha = primary.coverage;
            const Vec3f n = primary.isect.normal;
            out.normal = Color3f(n.x, n.y, n.z);
            return out;
        }
        const Vec3f n = primary.isect.normal;
        out.normal = Color3f(n.x, n.y, n.z);
        out.alpha = 1.0f;
    } else {
        // OIDN için ıskalayan pikselin albedosu: görünen arka planın rengi (≤ 1).
        const EnvironmentLight* env = scene.environment();
        out.albedo = clamp01(bg.mode == Background::Mode::Color ? bg.color
                             : env ? env->eval(ray.direction) : Color3f::black());
        out.normal = Color3f(0.0f);
        out.alpha = 0.0f;
    }
    return out;
}

void addPasses(RenderPassFilms* passes, int x, int y, const PixelSample& s) {
    if (!passes) return;
    for (int k = 0; k < static_cast<int>(LightPass::Count); ++k) passes->light[k].addSample(x, y, s.light.c[k]);
    passes->depth.addSample(x, y, Color3f(s.depth));
    passes->position.addSampleSigned(x, y, Color3f(s.position.x, s.position.y, s.position.z));
    passes->objectId.addSample(x, y, s.objectId);
    passes->materialId.addSample(x, y, s.materialId);
    const size_t nGroups = std::min(passes->lightGroups.size(), static_cast<size_t>(LightPasses::kMaxLightGroups));
    for (size_t g = 0; g < nGroups; ++g) passes->lightGroups[g].second.addSample(x, y, s.light.lightGroups[g]);
    if (passes->cryptoObject.width() > 0) passes->cryptoObject.addSample(x, y, s.cryptoObjHash);
    if (passes->cryptoMaterial.width() > 0) passes->cryptoMaterial.addSample(x, y, s.cryptoMatHash);
}

void addAovs(AovFilms* aovs, int x, int y, const PixelSample& s) {
    if (!aovs) return;
    aovs->albedo.addSample(x, y, s.albedo);
    aovs->normal.addSampleSigned(x, y, s.normal);
    aovs->alpha.addSample(x, y, Color3f(s.alpha));
}

void denoiseWithAovs(Image& out, const AovFilms* aovs) {
    if (aovs && denoiseAvailable()) {
        Image albedo = aovs->albedo.resolve();
        Image normal = aovs->normal.resolve();
        denoiseImage(out, &albedo, &normal);
    } else {
        denoiseImage(out);
    }
}

bool lightGroupsMatch(const RenderPassFilms& passes, const std::vector<std::string>& names, int w, int h) {
    if (passes.lightGroups.size() != names.size()) return false;
    for (size_t i = 0; i < names.size(); ++i) {
        if (passes.lightGroups[i].first != names[i]) return false;
        if (passes.lightGroups[i].second.width() != w || passes.lightGroups[i].second.height() != h) return false;
    }
    return true;
}

} // namespace

Renderer::Renderer() = default;
Renderer::~Renderer() = default;

// Owen karıştırmalı Sobol (Burley 2020): her piksel ve boyut kendi karıştırmasını
// alır, ilerlemeli render'da her ön-ek (1, 2, 4... örnek) iyi tabakalanır.
Color3f idColor(uint64_t id) {
    if (id == 0) return Color3f::black();
    // splitmix64 → [0,1) ton; altın oran adımı komşu kimlikleri belirgin ayırır.
    uint64_t z = id + 0x9e3779b97f4a7c15ull;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    z ^= z >> 31;
    const float h = std::fmod(static_cast<float>(z >> 40) * 0x1p-24f + 0.618034f, 1.0f) * 6.0f;
    const int i = static_cast<int>(h) % 6;
    const float f = h - std::floor(h);
    const float v = 1.0f, s = 0.75f, p0 = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
    switch (i) {
        case 0: return Color3f(v, t, p0);
        case 1: return Color3f(q, v, p0);
        case 2: return Color3f(p0, v, t);
        case 3: return Color3f(p0, q, v);
        case 4: return Color3f(t, p0, v);
        default: return Color3f(v, p0, q);
    }
}

std::vector<ExrLayer> makeExrLayers(const RenderPassFilms* passes, const AovFilms* aovs,
                                    std::vector<Image>& storage,
                                    std::vector<CryptoRankImage>* cryptoStorage) {
    std::vector<ExrLayer> layers;
    storage.clear();
    const size_t nGroups = passes ? passes->lightGroups.size() : 0;
    storage.reserve(static_cast<size_t>(LightPass::Count) + 8 + nGroups); // işaretçiler geçerli kalsın
    auto add = [&](const std::string& name, const Film& f, const char* suffixes, bool full) {
        if (f.width() <= 0) return;
        storage.push_back(f.resolve());
        layers.push_back(ExrLayer{name, &storage.back(), suffixes, full, nullptr});
    };
    if (passes) {
        for (int k = 0; k < static_cast<int>(LightPass::Count); ++k)
            add(lightPassName(static_cast<LightPass>(k)), passes->light[k], "RGB", false);
        for (const auto& [groupName, film] : passes->lightGroups)
            add("LG_" + groupName, film, "RGB", false);
        add("Depth", passes->depth, "Z", true);
        add("Position", passes->position, "XYZ", true);
        add("ObjectID", passes->objectId, "RGB", false);
        add("MaterialID", passes->materialId, "RGB", false);
    }
    if (aovs) {
        add("DiffCol", aovs->albedo, "RGB", false);
        add("Normal", aovs->normal, "XYZ", false);
    }
    if (passes && cryptoStorage && passes->cryptoObject.width() > 0 && passes->cryptoObject.height() > 0) {
        cryptoStorage->clear();
        auto objRanks = passes->cryptoObject.resolveRanks(3);
        auto matRanks = passes->cryptoMaterial.resolveRanks(3);
        cryptoStorage->reserve(objRanks.size() + matRanks.size());
        auto pushCrypto = [&](const std::string& base, std::vector<CryptoRankImage>& ranks) {
            for (size_t i = 0; i < ranks.size(); ++i) {
                char idx[32];
                std::snprintf(idx, sizeof(idx), "%02u", static_cast<unsigned>(i % 100u));
                cryptoStorage->push_back(std::move(ranks[i]));
                layers.push_back(ExrLayer{base + idx, nullptr, "RGBA", true, &cryptoStorage->back().rgba});
            }
        };
        pushCrypto("CryptoObject", objRanks);
        pushCrypto("CryptoMaterial", matRanks);
    }
    return layers;
}

std::vector<std::pair<std::string, std::string>> makeExrCryptoMetadata(const RenderPassFilms* passes) {
    std::vector<std::pair<std::string, std::string>> meta;
    if (!passes || passes->cryptoObject.width() <= 0 || passes->cryptoObject.height() <= 0) return meta;
    auto obj = passes->cryptoObject.exrMetadata("CryptoObject");
    auto mat = passes->cryptoMaterial.exrMetadata("CryptoMaterial");
    meta.insert(meta.end(), obj.begin(), obj.end());
    meta.insert(meta.end(), mat.begin(), mat.end());
    return meta;
}

std::unique_ptr<Sampler> createRenderSampler(int samplesPerPixel, uint64_t seed) {
    (void)samplesPerPixel;
    return createSobolSampler(seed);
}

Image Renderer::render(const Scene& scene, const Camera& camera, const RenderSettings& settings,
                       const std::atomic<bool>* cancel, RenderPassFilms* passes, AovFilms* aovsOut) {
    Film film(settings.width, settings.height);
    AovFilms localAovs;
    AovFilms* aovs = aovsOut ? aovsOut : (settings.denoiseEnabled ? &localAovs : nullptr);
    if (aovs) aovs->resize(settings.width, settings.height);
    if (passes) passes->resize(settings.width, settings.height);

    const auto startTime = std::chrono::high_resolution_clock::now();
    const int spp = std::max(1, settings.samplesPerPixel);
    const SampleBatchResult r = renderSamples(scene, camera, settings, film, 0, spp, cancel, aovs, passes);
    if (settings.adaptiveSampling)
        std::cout << "Uyarlamalı: ortalama "
                  << static_cast<double>(r.samplesTraced) / (static_cast<double>(settings.width) * settings.height)
                  << " örnek/piksel (hedef " << spp << ")" << std::endl;

    Image out = film.resolve();
    if (film.rejectedSamples() > 0) {
        std::cerr << "Rejected " << film.rejectedSamples() << " non-finite or negative samples." << std::endl;
    }
    if (settings.denoiseEnabled && r.completed) denoiseWithAovs(out, aovs);

    std::chrono::duration<double> elapsed = std::chrono::high_resolution_clock::now() - startTime;
    std::cout << "Render " << settings.width << "x" << settings.height << " @ " << spp << " spp: " << elapsed.count()
              << " s" << std::endl;
    return out;
}

Image Renderer::renderProgressive(const Scene& scene, const Camera& camera, const RenderSettings& settings,
                                  const std::function<bool(const Film&, int)>& onPass,
                                  AovFilms* aovsOut, RenderPassFilms* passes) {
    Film film(settings.width, settings.height);
    AovFilms localAovs;
    AovFilms* aovs = aovsOut ? aovsOut : (settings.denoiseEnabled ? &localAovs : nullptr);
    if (aovs) aovs->resize(settings.width, settings.height);

    bool cancelled = false;
    for (int s = 0; s < settings.samplesPerPixel; ++s) {
        renderSamplePass(scene, camera, settings, film, s, nullptr, aovs, passes);
        if (onPass && !onPass(film, s + 1)) {
            cancelled = true;
            break;
        }
    }

    Image out = film.resolve();
    if (settings.denoiseEnabled && !cancelled) denoiseWithAovs(out, aovs);
    return out;
}

bool Renderer::renderSamplePass(const Scene& scene, const Camera& camera, const RenderSettings& settings,
                                Film& accum, int passIndex, const std::atomic<bool>* cancel,
                                AovFilms* aovs, RenderPassFilms* passes) {
    if (accum.width() != settings.width || accum.height() != settings.height) {
        accum.resize(settings.width, settings.height);
    }
    return renderSamples(scene, camera, settings, accum, passIndex, 1, cancel, aovs, passes).completed;
}

SampleBatchResult Renderer::renderSamples(const Scene& scene, const Camera& camera, const RenderSettings& settings,
                                          Film& accum, int firstSample, int count, const std::atomic<bool>* cancel,
                                          AovFilms* aovs, RenderPassFilms* passes) {
    SampleBatchResult result;
    // Çağıranlar RenderSettings'i eşzamanlı değiştirebilir; boyutlar filmden alınır.
    const int w = accum.width();
    const int h = accum.height();
    if (w <= 0 || h <= 0 || count <= 0) return result;
    if (aovs && (aovs->albedo.width() != w || aovs->albedo.height() != h)) aovs->resize(w, h);
    if (passes) {
        if (passes->depth.width() != w || passes->depth.height() != h) passes->resize(w, h);
        if (!lightGroupsMatch(*passes, scene.lightGroupNames(), w, h)) {
            passes->initLightGroups(w, h, scene.lightGroupNames());
        }
    }

    std::unique_ptr<CryptoLookup> cryptoLookup;
    if (passes) {
        cryptoLookup = std::make_unique<CryptoLookup>(buildCryptoLookup(scene, *passes));
    }

    // SD-Tree Path Guiding: ilk 32 örnekte artan dalgalarla (2, 4, 8, 16, 32) eğitilip
    // arıtılır, ardından dondurulmuş örnekleme ağacıyla saf yönlendirmeli render sürer.
    if (settings.pathGuiding) {
        if (!m_guiding || m_guidingScene != &scene || firstSample == 0) {
            m_guiding = std::make_unique<GuidingField>(scene.bounds());
            m_guidingScene = &scene;
            m_guidingSamples = 0;
            m_guidingNextRefine = 2;
            m_guidingIter = 0;
        }
    } else if (m_guiding) {
        m_guiding.reset();
        m_guidingScene = nullptr;
        m_guidingSamples = 0;
        m_guidingNextRefine = 2;
        m_guidingIter = 0;
    }

    std::unique_ptr<Sampler> baseSampler = createRenderSampler(settings.samplesPerPixel, 12345);
    PathTracer integrator(settings.maxBounces, 3, settings.aoStrength, settings.shadowQuality);
    integrator.setClampIndirect(settings.clampIndirect);
    const PixelFilterSampler filter(settings.filter);
    std::unique_ptr<ThreadPool> ownedPool;
    ThreadPool* pool = makePoolOverride(settings, ownedPool);

    // Uyarlamalı örnekleme: kendisi ve 3×3 komşuları gürültü eşiğinin altına inen piksel bu
    // partiyi atlar (komşuluk: temiz bölgenin içinde gürültülü tek piksel kalmasın). Parti
    // içinde de her piksel en az minAdaptiveSamples aldıktan sonra 8 örnekte bir kendi
    // hatasına bakar; bütçe kenarlara, kostiklere ve gölgelere gider, düz fon erken biter.
    const float threshold = std::max(1e-4f, settings.noiseThreshold);
    const int minSpp = std::max(2, settings.minAdaptiveSamples);
    const bool adaptive = settings.adaptiveSampling;
    std::vector<uint8_t> skipMask;
    if (adaptive && firstSample >= minSpp) {
        std::vector<uint8_t> clean(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
        parallelFor2D(w, h, [&](int xBegin, int xEnd, int yBegin, int yEnd) {
            for (int y = yBegin; y < yEnd; ++y)
                for (int x = xBegin; x < xEnd; ++x)
                    clean[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] =
                        accum.relativeError(x, y) < threshold ? 1 : 0;
        }, 64, pool, cancel);
        skipMask.assign(clean.size(), 0);
        parallelFor2D(w, h, [&](int xBegin, int xEnd, int yBegin, int yEnd) {
            for (int y = yBegin; y < yEnd; ++y)
                for (int x = xBegin; x < xEnd; ++x) {
                    bool all = true;
                    for (int dy = -1; dy <= 1 && all; ++dy)
                        for (int dx = -1; dx <= 1 && all; ++dx) {
                            const int nx = std::clamp(x + dx, 0, w - 1), ny = std::clamp(y + dy, 0, h - 1);
                            all = clean[static_cast<size_t>(ny) * static_cast<size_t>(w) + static_cast<size_t>(nx)] != 0;
                        }
                    skipMask[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = all ? 1 : 0;
                }
        }, 64, pool, cancel);
    }

    const bool wantAovs = aovs != nullptr;
    const bool wantPasses = passes != nullptr;
    const int endSample = firstSample + count;
    std::atomic<uint64_t> traced{0};

    int curSample = firstSample;
    while (curSample < endSample) {
        int chunkEnd = endSample;
        if (settings.pathGuiding && m_guiding && m_guidingSamples < 32) {
            const int untilRefine = std::max(1, m_guidingNextRefine - m_guidingSamples);
            chunkEnd = std::min(endSample, curSample + untilRefine);
            integrator.setGuiding(m_guiding->ready() ? m_guiding.get() : nullptr, m_guiding.get());
        } else if (settings.pathGuiding && m_guiding) {
            integrator.setGuiding(m_guiding->ready() ? m_guiding.get() : nullptr, nullptr);
        } else {
            integrator.setGuiding(nullptr, nullptr);
        }

        const bool completed = parallelFor2D(w, h, [&](int xBegin, int xEnd, int yBegin, int yEnd) {
            // Sobol karıştırması partiler boyunca SABİT kalmalı; yoksa ardışık örnekler
            // aynı dizinin devamı olmaz ve tabakalanma kaybolur. Piksel ayrışması içeride.
            const uint64_t tileSeed = 12345; // karo boyutundan bağımsız, deterministik
            std::unique_ptr<Sampler> tileSampler = baseSampler->clone(tileSeed);
            uint64_t local = 0;
            for (int y = yBegin; y < yEnd; ++y) {
                // Satır başında iptal kontrolü: büyük karolarda bile tepki ~ms.
                if (cancel && cancel->load(std::memory_order_relaxed)) break;
                for (int x = xBegin; x < xEnd; ++x) {
                    if (!skipMask.empty() && skipMask[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)])
                        continue;
                    for (int s = curSample; s < chunkEnd; ++s) {
                        PixelSample ps = tracePixel(scene, camera, integrator, filter, *tileSampler, x, y, s, w, h,
                                                    wantAovs, wantPasses, settings.motionBlur, cryptoLookup.get());
                        accum.addSample(x, y, ps.L);
                        if (wantAovs) addAovs(aovs, x, y, ps);
                        if (wantPasses) addPasses(passes, x, y, ps);
                        ++local;
                        if (adaptive && s + 1 < chunkEnd && s + 1 >= minSpp && ((s + 1) & 7) == 0 &&
                            accum.relativeError(x, y) < threshold)
                            break;
                    }
                }
            }
            traced.fetch_add(local, std::memory_order_relaxed);
        }, settings.tileSize, pool, cancel);

        if (!completed) {
            result.completed = false;
            break;
        }
        if (settings.pathGuiding && m_guiding && m_guidingSamples < 32) {
            m_guidingSamples += (chunkEnd - curSample);
            if (m_guidingSamples >= m_guidingNextRefine) {
                m_guiding->refine(++m_guidingIter);
                m_guidingNextRefine = m_guidingNextRefine < 32 ? m_guidingNextRefine * 2 : 64;
            }
        }
        curSample = chunkEnd;
    }

    result.samplesTraced = traced.load();
    return result;
}

} // namespace photon
