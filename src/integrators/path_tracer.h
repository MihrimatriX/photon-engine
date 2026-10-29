// Yol izleyici integratörün arayüzü. maxDepth = en fazla saçılma (sekme) sayısı
// (PBRT kuralı): 1 → yalnızca doğrudan ışık, 2 → bir sekme dolaylı ışık, ...
#pragma once

/// @file path_tracer.h
/// @brief Multi-bounce Path Tracer integrator in PhotonEngine.

#include "integrators/integrator.h"
#include "geometry/surface_interaction.h"
#include "materials/material.h"

namespace photon {

/// Işık geçişleri (render passes, compositing): her örneğin katkısı yolun ilk yüzeyindeki lob
/// grubuna (difüz / parlak / geçirgen) ve doğrudan (ışığa ilk yüzeyden ulaşan) ya da dolaylı
/// olmasına göre ayrılır; kameranın doğrudan gördüğü yayıcılar Emission, ortam/arka plan
/// Background'a gider. Geçişlerin toplamı beauty'dir (yuvarlama hariç). İlk yüzeydeki lob payı
/// f_k / f oranıyla verilir: her örnek tüm lob geçişlerine katkı verir, tahmin yansızdır.
enum class LightPass : int {
    DiffuseDirect, DiffuseIndirect, GlossyDirect, GlossyIndirect,
    TransmissionDirect, TransmissionIndirect, Emission, Background, Count
};

struct LightPasses {
    Color3f c[static_cast<int>(LightPass::Count)];
};

/// Geçişin çok katmanlı EXR'deki adı (Cycles/Blender adlandırması).
const char* lightPassName(LightPass pass);

/// @brief Path tracer supporting global illumination, direct lighting, and Russian roulette.
/// @p maxDepth counts scattering events; emission found by the last scattered ray is still added.
class PathTracer : public Integrator {
public:
    explicit PathTracer(int maxDepth = 8, int russianRouletteDepth = 3,
                        float aoStrength = 0.0f, int shadowQuality = 1)
        : m_maxDepth(maxDepth)
        , m_russianRouletteDepth(russianRouletteDepth)
        , m_aoStrength(aoStrength)
        , m_shadowQuality(shadowQuality < 1 ? 1 : shadowQuality) {}

    /// Kamera ışınının ilk kesişimi (gürültü giderici AOV'leri ve seçim için).
    struct PrimaryHit {
        bool hit = false;
        SurfaceInteraction isect;
        /// Kamera ışını gölge yakalayıcıya çarptıysa ≥ 0: gölgenin opaklığı (1 − görünürlük).
        /// Şeffaf arka planda alfa budur; renk kanalı arka plan × görünürlüktür.
        float catcherAlpha = -1.0f;
        Color3f catcherBackground; ///< yakalayıcının yerine görünen arka plan (AOV albedo)
        /// ≥ 0: kamera ışını ince duvarlı camlardan düz geçip şeffaf arka plana ulaştı;
        /// kapsama (alfa) = camın geçirmediği oran.
        float coverage = -1.0f;
    };

    Color3f Li(const Ray& ray, const Scene& scene, Sampler& sampler) const override {
        return Li(ray, scene, sampler, nullptr);
    }

    /// @p ray bir KAMERA ışınıysa ve @p primary verilmişse ilk kesişim oraya yazılır,
    /// sahnenin arka plan modu (düz renk / şeffaf) ıskalayan kamera ışınına uygulanır.
    /// Böylece AOV için ayrı bir birincil ışın atmaya gerek kalmaz.
    Color3f Li(const Ray& ray, const Scene& scene, Sampler& sampler, PrimaryHit* primary,
               LightPasses* passes = nullptr) const;

    /// Yüzey altı rastgele yürüyüşü (Chiang 2016 / Cycles): @p entry noktasından @p dir
    /// yönünde cismin içine girilir; homojen ortamda serbest uçuş + Henyey–Greenstein
    /// saçılmalarıyla ilerlenir, bir yüzeye ulaşınca çıkılır. Çıkış bulunursa true döner,
    /// @p exitSi o noktanın kesişim kaydıdır (normal İÇE bakar), @p throughput yürüyüşün
    /// ağırlığıyla çarpılmıştır. Kaçış (açık mesh) ya da soğrulma → false.
    static bool randomWalkSubsurface(const Scene& scene, const SurfaceInteraction& entry, const Vec3f& dir,
                                     const SubsurfaceCoeffs& coeffs, Sampler& sampler, Color3f& throughput,
                                     SurfaceInteraction& exitSi);

    /// Test kancası: yuvarlatılmış kenar normali (integratörün ilk köşelerde kullandığı hesap).
    static Vec3f roundedEdgeNormalForTest(const Scene& scene, const SurfaceInteraction& isect, float radius,
                                          Sampler& sampler);

    /// Dolaylı katkıları (ışık ikinci ve sonraki köşelerden bulunduysa) en büyük bileşeni
    /// @p limit olacak şekilde kırpar. 0 = kapalı. Yanlıdır ama kostik parlamaları söndürür.
    void setClampIndirect(float limit) { m_clampIndirect = limit > 0.0f ? limit : 0.0f; }

private:
    float m_clampIndirect = 0.0f;
    int m_maxDepth;
    int m_russianRouletteDepth;
    float m_aoStrength;
    int m_shadowQuality;
};

} // namespace photon
