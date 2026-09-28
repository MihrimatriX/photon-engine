#pragma once

/// @file render_settings.h
/// @brief Global render configuration structure for PhotonEngine.

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

    ToneMapOperator tmo = ToneMapOperator::ACES; ///< Tone mapping operator to apply
    float exposure = 0.0f;             ///< Exposure value (EV stops)

    bool adaptiveSampling = false;     ///< Extra samples on high-variance pixels
    bool denoiseEnabled = false;       ///< Apply OIDN when available; else soft blur fallback
    float aoStrength = 0.0f;           ///< Contact AO strength (short-range; not full GI)
    int shadowQuality = 1;             ///< NEE / AO sample multiplier (1–4)
};

} // namespace photon
