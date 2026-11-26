// hybrid_device.h — Eşzamanlı Hibrit GPU + CPU render cihazı.
//
// Bir örnek partisini [firstSample, firstSample + gpuCount) ve [firstSample + gpuCount, firstSample + count)
// ayrık dilimlerine bölerek Vulkan GPU ile CPU'yu aynı anda çalıştırır; dinamik EMA hız
// oranıyla iş yükünü otomatik dengeler ve Film / AovFilms / RenderPassFilms sonuçlarını birleştirir.
#pragma once

#include "engine/render_device.h"

#include <memory>

namespace photon {

class HybridDevice final : public RenderDevice {
public:
    HybridDevice(DeviceInfo info, std::unique_ptr<RenderDevice> gpu, std::unique_ptr<RenderDevice> cpu);

    const DeviceInfo& info() const override { return m_info; }
    DeviceFeatures features() const override;

    void setScene(std::shared_ptr<const Scene> scene) override;
    void updateScene(std::shared_ptr<const Scene> scene, SceneUpdateKind kind) override;
    bool hasScene() const override { return m_gpu && m_gpu->hasScene() && m_cpu && m_cpu->hasScene(); }

    SampleBatchResult render(const Camera& camera, const RenderSettings& settings, RenderTargets& targets,
                             int firstSample, int count, const std::atomic<bool>* cancel) override;

    double gpuShare() const { return m_gpuShare; }

private:
    DeviceInfo m_info;
    std::unique_ptr<RenderDevice> m_gpu;
    std::unique_ptr<RenderDevice> m_cpu;
    std::shared_ptr<const Scene> m_scene;
    double m_gpuShare = 0.65;
};

} // namespace photon
