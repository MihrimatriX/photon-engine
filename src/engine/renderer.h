// renderer.h — Karo tabanlı, çok iş parçacıklı render sürücüsü.
//
// Renderer kameradan her piksel için ışın üretir, integrator'e (path tracer)
// verir ve sonucu Film'e biriktirir. Temel işlem renderSamples(): her piksele
// [first, first + count) aralığındaki örnekler, karo karo (karo önce, sonra örnek:
// sahnenin o bölgesi önbellekte kalır; 1 örneklik pass'lere göre ~%7 hızlı). Örnekleyici
// (piksel, örnek indeksi)'ne göre deterministik olduğundan 8 örneklik bir parti, sekiz
// tek örneklik pass ile aynı görüntüyü verir. Üstündeki yardımcılar:
//   * render():            tüm örnekler tek partide (testler, basit kullanım)
//   * renderSamplePass():  piksel başına 1 örnek (viewport'un ilerlemeli pass'i)
// Uygulama ve CLI bunları doğrudan değil, RenderDevice + RenderSession üzerinden kullanır.
// Partiler `cancel` bayrağıyla satır düzeyinde durdurulabilir.
#pragma once

#include "engine/scene.h"
#include "engine/render_settings.h"
#include "camera/camera.h"
#include "core/image/cryptomatte.h"
#include "core/image/film.h"
#include "core/image/image.h"
#include "core/image/image_io.h"
#include "integrators/path_tracer.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace photon {

class Sampler;
class GuidingField;

/// Üretim örnekleyicisi. Testler IndependentSampler kullanmaya devam eder.
std::unique_ptr<Sampler> createRenderSampler(int samplesPerPixel, uint64_t seed = 12345);

/// Gürültü gidericinin (OIDN) yardımcı kanalları. Her biri beauty ile aynı
/// alt-piksel (jitter) konumundan, ilk kesişimde toplanır; böylece kenarlar
/// beauty ile birebir hizalanır.
struct AovFilms {
    Film albedo;  ///< İlk yüzeyin taban rengi (ıskalayan ışında arka plan, ≤ 1).
    Film normal;  ///< İlk yüzeyin dünya uzayı normali (negatif bileşen olabilir).
    Film alpha;   ///< Kapsama: ışın geometriye çarptıysa 1, ıskaladıysa 0 (r kanalında).

    void resize(int w, int h) { albedo.resize(w, h); normal.resize(w, h); alpha.resize(w, h); }
    void merge(const AovFilms& other) {
        albedo.merge(other.albedo);
        normal.merge(other.normal);
        alpha.merge(other.alpha);
    }
};

/// Render geçişleri (çok katmanlı EXR, compositing). Işık geçişleri toplanınca beauty'yi verir
/// (PathTracer::LightPasses); veri geçişleri ilk kesişimden: kameraya uzaklık, dünya konumu,
/// nesne ve malzeme kimliği renkleri (Nuke/AE'de maske için). Her biri beauty ile aynı alt-piksel
/// konumundan biriktirilir. Bellek: geçiş başına piksel ~20 bayt (1080p'de ~500 MB toplam).
struct RenderPassFilms {
    Film light[static_cast<int>(LightPass::Count)];
    Film depth;
    Film position;
    Film objectId;
    Film materialId;
    std::vector<std::pair<std::string, Film>> lightGroups; ///< Işık grupları (EXR LG_<ad>)
    CryptoFilm cryptoObject;                               ///< Psyop Cryptomatte 1.2.0 (CryptoObject)
    CryptoFilm cryptoMaterial;                             ///< Psyop Cryptomatte 1.2.0 (CryptoMaterial)

    void initLightGroups(int w, int h, const std::vector<std::string>& names) {
        lightGroups.clear();
        lightGroups.reserve(names.size());
        for (const std::string& n : names) lightGroups.emplace_back(n, Film(w, h));
    }

    void resize(int w, int h) {
        for (Film& f : light) f.resize(w, h);
        depth.resize(w, h);
        position.resize(w, h);
        objectId.resize(w, h);
        materialId.resize(w, h);
        for (auto& [name, f] : lightGroups) f.resize(w, h);
        cryptoObject.resize(w, h);
        cryptoMaterial.resize(w, h);
    }

    void merge(const RenderPassFilms& other) {
        for (int k = 0; k < static_cast<int>(LightPass::Count); ++k) light[k].merge(other.light[k]);
        depth.merge(other.depth);
        position.merge(other.position);
        objectId.merge(other.objectId);
        materialId.merge(other.materialId);
        if (lightGroups.size() == other.lightGroups.size()) {
            for (size_t i = 0; i < lightGroups.size(); ++i) lightGroups[i].second.merge(other.lightGroups[i].second);
        }
        cryptoObject.merge(other.cryptoObject);
        cryptoMaterial.merge(other.cryptoMaterial);
    }
};

/// Bir kimliği (düğüm, malzeme) ayırt edilebilir, kararlı bir renge çevirir (altın oran tonu).
Color3f idColor(uint64_t id);

/// Geçişleri ve AOV'leri çözüp çok katmanlı EXR katmanlarına çevirir (Cycles/Blender adları:
/// DiffDir, GlossInd, Emit, Env, Depth, Normal, DiffCol, LG_*, CryptoObject00..02, CryptoMaterial00..02).
/// Görüntüler @p storage ve (verilirse) @p cryptoStorage içinde tutulur; katmanlardan uzun yaşamalıdır.
std::vector<ExrLayer> makeExrLayers(const RenderPassFilms* passes, const AovFilms* aovs,
                                    std::vector<Image>& storage,
                                    std::vector<CryptoRankImage>* cryptoStorage = nullptr);

/// Cryptomatte OpenEXR metin üstverisini (cryptomatte/<7hex>/{name,hash,conversion,manifest}) üretir.
std::vector<std::pair<std::string, std::string>> makeExrCryptoMetadata(const RenderPassFilms* passes);

/// Bir partinin sonucu.
struct SampleBatchResult {
    bool completed = true;      ///< false: iptal edildi (film yarım; piksel başına ortalama yine geçerli)
    uint64_t samplesTraced = 0; ///< izlenen örnek sayısı (uyarlamalıda atlanan pikseller sayılmaz)
};

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /// Her piksele [firstSample, firstSample + count) örnekleri ekler. Uyarlamalı örneklemede
    /// komşuluğuyla birlikte eşiğin altına inen piksel atlanır, parti içinde de 8 örnekte bir
    /// kontrol edilip erken durur. Film/AOV/geçiş boyutları ayarlardan değil filmden alınır.
    SampleBatchResult renderSamples(const Scene& scene, const Camera& camera, const RenderSettings& settings,
                                    Film& accum, int firstSample, int count, const std::atomic<bool>* cancel = nullptr,
                                    AovFilms* aovs = nullptr, RenderPassFilms* passes = nullptr);

    /// Senkron render: her karo için tüm örnekler. Ortalaması alınmış HDR görüntü
    /// döner; ayarlar isterse gürültüsü giderilmiş olur.
    /// @p passes / @p aovsOut doluysa render geçişleri ve AOV'ler (albedo, normal, alfa) de yazılır.
    Image render(const Scene& scene, const Camera& camera, const RenderSettings& settings,
                 const std::atomic<bool>* cancel = nullptr, RenderPassFilms* passes = nullptr,
                 AovFilms* aovsOut = nullptr);

    /// Piksel başına birer örneklik pass'lerle samplesPerPixel'e kadar render.
    /// @param onPass Her pass sonrası film ve şimdiye dek örnek sayısıyla çağrılır.
    ///               false dönerse render durur (iptal).
    /// @param aovsOut Doluysa AOV'ler buraya da yazılır (şeffaf PNG için alfa).
    Image renderProgressive(const Scene& scene, const Camera& camera, const RenderSettings& settings,
                            const std::function<bool(const Film&, int)>& onPass,
                            AovFilms* aovsOut = nullptr, RenderPassFilms* passes = nullptr);

    /// Var olan bir filme tek örneklik bir pass ekle.
    /// @return Pass tamamlandıysa true, iptal edildiyse false (film yarım kalır,
    ///         çağıran filmi atmalıdır).
    bool renderSamplePass(const Scene& scene, const Camera& camera, const RenderSettings& settings,
                          Film& accum, int passIndex, const std::atomic<bool>* cancel = nullptr,
                          AovFilms* aovs = nullptr, RenderPassFilms* passes = nullptr);

    /// Etkin SD-Tree Path Guiding alanı (test ve tanılama için).
    const GuidingField* guidingField() const { return m_guiding.get(); }

private:
    std::unique_ptr<GuidingField> m_guiding;
    const Scene* m_guidingScene = nullptr;
    int m_guidingSamples = 0;
    int m_guidingNextRefine = 2;
    int m_guidingIter = 0;
};

} // namespace photon
