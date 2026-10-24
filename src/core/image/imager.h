// imager.h — Görüntüleyici (imager): render'ın HDR sonucuna, ton eşlemeden ÖNCE uygulanan
// fotoğrafik son işlemler. Octane'nin "Imager"ı ve KeyShot'ın "Image Styles" karşılığı:
//   renk sapması → bloom → yıldız parlaması (glare) → beyaz dengesi → kontrast/doygunluk → vinyet
// Hepsi doğrusal (scene-linear) değerlerde çalışır; parlak noktalar gerçek HDR değerleriyle
// taştığı için bloom ve glare fiziksel bir merceğe benzer davranır. Varsayılanlar etkisizdir.
#pragma once

#include "core/image/image.h"

namespace photon {

struct ImagerSettings {
    // Bloom: parlak bölgelerin mercekte saçılan halesi.
    float bloomIntensity = 0.0f;  ///< 0 = kapalı
    float bloomRadius = 0.04f;    ///< görüntü köşegeninin oranı
    float bloomThreshold = 1.0f;  ///< pozlanmış parlaklık eşiği (1 = beyaz)
    // Glare: diyafram kanatlarının yıldız ışınları.
    float glareIntensity = 0.0f;  ///< 0 = kapalı
    int glareBlades = 6;          ///< ışın sayısı (çift sayıda iki yönlü çizgi)
    float glareAngleDeg = 15.0f;
    float glareLength = 0.1f;     ///< köşegenin oranı
    // Renk.
    float whiteBalanceK = 6500.0f; ///< sahne ışığının sıcaklığı: 6500 nötr, düşük = mavileştirir
    float tint = 0.0f;             ///< −1 yeşil … +1 macenta
    float saturation = 1.0f;
    float contrast = 1.0f;         ///< orta gri (0.18) etrafında log-kontrast
    // Lens.
    float vignette = 0.0f;              ///< 0..1, köşelerde kararma
    float chromaticAberration = 0.0f;   ///< köşedeki kanal kayması (köşegenin binde biri)

    bool isIdentity() const {
        return bloomIntensity <= 0.0f && glareIntensity <= 0.0f && whiteBalanceK == 6500.0f && tint == 0.0f &&
               saturation == 1.0f && contrast == 1.0f && vignette <= 0.0f && chromaticAberration <= 0.0f;
    }
};

/// @p hdr yerinde işlenir. @p exposureEV bloom/glare eşiğini ekrandaki parlaklığa bağlar.
void applyImager(Image& hdr, const ImagerSettings& s, float exposureEV = 0.0f);

/// Beyaz dengesi matrisi (doğrusal sRGB, satır öncelikli 3×3): T sıcaklığındaki ışık
/// kaynağının beyazını D65'e taşır (Bradford uyarlaması; T ≥ 4000 K gün ışığı eğrisi).
void whiteBalanceMatrix(float temperatureK, float tint, float out[9]);

} // namespace photon
