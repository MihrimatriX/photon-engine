// render_session.cpp — Mod politikası: parti boyu, durma koşulları, çıktıların çözülmesi.
#include "engine/render_session.h"

#include <algorithm>
#include <cmath>

namespace photon {

const char* renderModeName(RenderMode mode) {
    switch (mode) {
        case RenderMode::Interactive: return "Etkileşimli";
        case RenderMode::Final: return "Son render";
    }
    return "?";
}

void RenderSession::begin(std::shared_ptr<const Camera> camera, const RenderSettings& settings,
                          const SessionConfig& config) {
    m_camera = std::move(camera);
    m_settings = settings;
    m_config = config;
    m_warnings.clear();
    const DeviceFeatures f = m_device.features();
    if (m_config.aovs && !f.aovs) {
        m_config.aovs = false;
        m_warnings.push_back(m_device.info().name + ": AOV'ler (albedo/normal/alfa) desteklenmiyor; gürültü giderme yalnız renkle");
    }
    if (m_config.passes && !f.lightPasses) {
        m_config.passes = false;
        m_warnings.push_back(m_device.info().name + ": render geçişleri desteklenmiyor; yalnız beauty yazılacak");
    }
    if (m_settings.adaptiveSampling && !f.adaptiveSampling) {
        m_settings.adaptiveSampling = false;
        m_warnings.push_back(m_device.info().name + ": uyarlamalı örnekleme desteklenmiyor; tüm pikseller hedefe kadar");
    }
    const int w = std::max(1, settings.width), h = std::max(1, settings.height);
    m_film.resize(w, h);
    if (m_config.aovs) m_aovs.resize(w, h);
    else m_aovs = AovFilms{};
    if (m_config.passes) {
        m_passes = std::make_unique<RenderPassFilms>();
        m_passes->resize(w, h);
    } else {
        m_passes.reset();
    }
    m_samples = 0;
    m_traced = 0;
    m_converged = false;
    m_secondsPerSample = 0.0;
    m_lastBatchSeconds = 0.0;
    m_start = Clock::now();
}

double RenderSession::seconds() const {
    return std::chrono::duration<double>(Clock::now() - m_start).count();
}

double RenderSession::averageSamples() const {
    const double px = static_cast<double>(m_film.width()) * static_cast<double>(m_film.height());
    return px > 0.0 ? static_cast<double>(m_traced) / px : 0.0;
}

bool RenderSession::finished() const {
    if (m_converged) return true;
    if (m_config.targetSpp > 0 && m_samples >= m_config.targetSpp) return true;
    return m_config.timeLimit > 0.0 && m_samples > 0 && seconds() >= m_config.timeLimit;
}

int RenderSession::nextBatch() const {
    // Etkileşimli: her pass ekrana gelir, kamera değişince en geç bir pass sonra tepki verilir.
    if (m_config.mode == RenderMode::Interactive || m_samples == 0 || m_secondsPerSample <= 0.0) return 1;
    int n = static_cast<int>(m_config.batchSeconds / m_secondsPerSample);
    n = std::clamp(n, 1, std::max(1, m_config.maxBatch));
    if (m_config.targetSpp > 0) n = std::min(n, m_config.targetSpp - m_samples);
    if (m_config.timeLimit > 0.0) {
        const double left = m_config.timeLimit - seconds();
        n = std::min(n, std::max(1, static_cast<int>(left / m_secondsPerSample)));
    }
    return std::max(1, n);
}

bool RenderSession::advance(const std::atomic<bool>* cancel) {
    if (!m_camera || finished()) return false;
    RenderTargets targets;
    targets.beauty = &m_film;
    targets.aovs = m_config.aovs ? &m_aovs : nullptr;
    targets.passes = m_passes.get();
    const int count = nextBatch();
    const auto t0 = Clock::now();
    const SampleBatchResult r = m_device.render(*m_camera, m_settings, targets, m_samples, count, cancel);
    m_lastBatchSeconds = std::chrono::duration<double>(Clock::now() - t0).count();
    m_traced += r.samplesTraced;
    if (!r.completed) return false;
    m_samples += count;
    // Hız ölçümü (örnek/piksel başına süre): kısa partilerin gürültüsüne karşı yumuşatılır.
    const double perSample = m_lastBatchSeconds / count;
    m_secondsPerSample = m_secondsPerSample > 0.0 ? 0.5 * (m_secondsPerSample + perSample) : perSample;
    // Uyarlamalı: bir parti hiç örnek izlemediyse tüm pikseller (komşularıyla) eşiğin altında.
    if (m_settings.adaptiveSampling && r.samplesTraced == 0) m_converged = true;
    return true;
}

Image RenderSession::resolveDenoised(Denoiser& denoiser, Denoiser::Quality quality) const {
    Image out = m_film.resolve();
    if (m_config.aovs) {
        const Image albedo = m_aovs.albedo.resolve();
        const Image normal = m_aovs.normal.resolve();
        denoiser.run(out, &albedo, &normal, quality);
    } else {
        denoiser.run(out, nullptr, nullptr, quality);
    }
    return out;
}

Image RenderSession::resolveAlpha() const {
    if (m_config.aovs) return m_aovs.alpha.resolve();
    Image a(m_film.width(), m_film.height());
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) a.setPixel(x, y, Color3f(1.0f));
    return a;
}

} // namespace photon
