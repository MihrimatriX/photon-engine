// render_settings.h — Render kalite ve çıktı ayarları: çözünürlük, örnek sayısı, sekme
// sayısı, ton eşleme, pozlama, gürültü giderme. Viewport ve son render aynı yapıyı kullanır.
#pragma once

/// @file render_settings.h
/// @brief Global render configuration structure for PhotonEngine.

#include "core/image/pixel_filter.h"
#include "core/image/tone_mapping.h"

namespace photon {

/// @brief Settings controlling the render quality and output processing.
struct RenderSettings {
    int width = 512;                   ///< Render image width in pixels
    int height = 512;                  ///< Render image height in pixels
    int samplesPerPixel = 64;          ///< Target samples per pixel (higher = less noise)
    int maxBounces = 8;                ///< Maximum ray depth (path length)
    int tileSize = 32;                 ///< Size of tile for multi-threaded rendering (e.g. 32x32)
    int numThreads = 0;                ///< 0 = hardware_concurrency (full-render dialog override)

    ToneMapOperator tmo = ToneMapOperator::PBRNeutral; ///< Ton eşleme operatörü
    float exposure = 0.0f;             ///< Exposure value (EV stops)

    bool adaptiveSampling = false;     ///< Temizlenen pikseller erken durur (gürültü eşiği)
    float noiseThreshold = 0.02f;      ///< Uyarlamalı: göreli standart hata hedefi (0.01 çok temiz)
    int minAdaptiveSamples = 16;       ///< Uyarlamalı: karar vermeden önce en az bu kadar örnek
    /// Dolaylı katkı sınırı (parlama/"firefly" kesici). 0 = kapalı (yansız). Örn. 10:
    /// ikinci ve sonraki sekmelerden gelen tek bir örnek en fazla bu parlaklıkta olur.
    float clampIndirect = 0.0f;
    bool denoiseEnabled = false;       ///< Apply OIDN when available; else soft blur fallback
    float aoStrength = 0.0f;           ///< Contact AO strength (short-range; not full GI)
    int shadowQuality = 1;             ///< NEE / AO sample multiplier (1–4)
    /// Piksel yeniden yapılandırma filtresi (filtre önem örneklemesi). Kutu/1 = eski davranış.
    PixelFilter filter;
};

} // namespace photon
