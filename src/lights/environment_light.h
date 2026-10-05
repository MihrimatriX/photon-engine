// environment_light.h — Sonsuz uzaktaki ortam ışığı (HDRI / gökyüzü).
//
// Equirectangular (enlem-boylam) bir HDR görüntü sahneyi her yönden aydınlatır.
// Önem örnekleme (importance sampling) için görüntünün parlaklık × sin(θ)
// dağılımından 2B bir CDF kurulur: parlak pencereler/güneş daha sık seçilir,
// böylece aynı örnek sayısında çok daha az gürültü olur.
#pragma once

#include "lights/light.h"
#include "core/image/image.h"
#include "core/math/constants.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace photon {

/// Dünya yönünü equirect UV'ye çevirir. phi = atan2(-z, x) (CPU / path tracer kuralı).
/// u yatayda sarılır, v = θ/π ∈ [0, 1] (0 = zenit, 1 = nadir).
inline Vec2f directionToEquirect(const Vec3f& direction, float rotation = 0.0f) {
    Vec3f d = direction.normalized();
    float theta = std::acos(std::clamp(d.y, -1.0f, 1.0f));
    float phi = std::atan2(-d.z, d.x) + PI + rotation;
    phi = std::fmod(phi, TWO_PI);
    if (phi < 0.0f) phi += TWO_PI;
    return Vec2f(phi / TWO_PI, theta / PI);
}

/// Equirect görüntüde bilineer okuma: u sarılır, v kutuplarda kenetlenir.
/// (Image::sampleBilinear v'yi de sardığı için zenit ile nadir karışıyordu.)
Color3f sampleEquirect(const Image& img, float u, float v);

/// @brief Ortam haritasını temsil eden sonsuz alan ışığı. Değişmezdir (immutable):
/// döndürme ya da yoğunluk değişince yenisi kurulur; render thread'i eskisini
/// shared_ptr ile tuttuğu sürece eski nesne yaşar.
class EnvironmentLight : public Light {
public:
    /// @param envMap   Doğrusal (linear) HDR equirect görüntü. nullptr = düz gri.
    /// @param rotation Y ekseni etrafında döndürme (radyan).
    /// @param intensity Parlaklık çarpanı.
    EnvironmentLight(std::shared_ptr<const Image> envMap, float rotation, float intensity);

    LightSample sampleLi(const SurfaceInteraction& si, const Vec2f& sample) const override;
    bool isDelta() const override { return false; }
    Color3f power() const override;

    /// Verilen dünya yönündeki arka plan radyansı.
    Color3f eval(const Vec3f& direction) const;

    /// sampleLi'nin katı açı (solid angle) PDF'i. İki aşırı yükleme de parlaklık
    /// CDF'ini kullanır (normal kullanılmaz).
    float pdfLi(const Vec3f& wi) const;
    float pdfLi(const Vec3f& wi, const Vec3f& normal) const;

    /// Son marjinal CDF değeri. Haritada enerji varsa ~1, yoksa 0.
    float distributionIntegral() const {
        return m_marginalCdf.empty() ? 0.0f : m_marginalCdf.back();
    }

    const std::shared_ptr<const Image>& image() const { return m_envMap; }
    float rotation() const { return m_rotation; }
    float intensity() const { return m_intensity; }

private:
    void buildDistribution();
    float solidAnglePdf(const Vec3f& wi) const;

    std::shared_ptr<const Image> m_envMap;
    float m_rotation; // radyan
    float m_intensity;

    int m_width = 0;
    int m_height = 0;
    float m_funcInt = 0.0f;
    std::vector<float> m_func;          // piksel başına parlaklık * sin(theta)
    std::vector<float> m_marginalCdf;   // height + 1 eleman, önek toplamı, son = 1
    std::vector<float> m_condCdf;       // satır başına width + 1
};

} // namespace photon
