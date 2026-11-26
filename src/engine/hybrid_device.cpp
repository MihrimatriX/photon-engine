// hybrid_device.cpp — Eşzamanlı Vulkan GPU + CPU Hibrit render sürücüsü.
#include "engine/hybrid_device.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace photon {

HybridDevice::HybridDevice(DeviceInfo info, std::unique_ptr<RenderDevice> gpu, std::unique_ptr<RenderDevice> cpu)
    : m_info(std::move(info)), m_gpu(std::move(gpu)), m_cpu(std::move(cpu)) {}

DeviceFeatures HybridDevice::features() const {
    DeviceFeatures f;
    if (m_gpu && m_cpu) {
        const DeviceFeatures gf = m_gpu->features();
        const DeviceFeatures cf = m_cpu->features();
        f.aovs = gf.aovs && cf.aovs;
        f.lightPasses = gf.lightPasses && cf.lightPasses;
        f.adaptiveSampling = gf.adaptiveSampling && cf.adaptiveSampling;
    }
    return f;
}

void HybridDevice::setScene(std::shared_ptr<const Scene> scene) {
    m_scene = scene;
    if (m_gpu) m_gpu->setScene(scene);
    if (m_cpu) m_cpu->setScene(scene);
}

void HybridDevice::updateScene(std::shared_ptr<const Scene> scene, SceneUpdateKind kind) {
    m_scene = scene;
    if (m_gpu) m_gpu->updateScene(scene, kind);
    if (m_cpu) m_cpu->updateScene(scene, kind);
}

SampleBatchResult HybridDevice::render(const Camera& camera, const RenderSettings& settings, RenderTargets& targets,
                                       int firstSample, int count, const std::atomic<bool>* cancel) {
    if (!m_gpu || !m_cpu || !targets.beauty || count <= 0) return SampleBatchResult{false, 0};
    if (count == 1) {
        // Tek örneklik etkileşimli geçişte ikiye bölünemez: dönüşümlü veya GPU çalıştır.
        return (firstSample & 1) == 0 ? m_gpu->render(camera, settings, targets, firstSample, 1, cancel)
                                      : m_cpu->render(camera, settings, targets, firstSample, 1, cancel);
    }

    const int w = targets.beauty->width();
    const int h = targets.beauty->height();
    const int gpuCount = std::clamp(static_cast<int>(std::lround(static_cast<double>(count) * m_gpuShare)), 1, count - 1);
    const int cpuCount = count - gpuCount;

    // CPU için geçici hedef tamponları (paralel yazım çakışmasız; bitince merge edilir).
    Film cpuBeauty(w, h);
    AovFilms cpuAovs;
    RenderPassFilms cpuPasses;
    RenderTargets cpuTargets;
    cpuTargets.beauty = &cpuBeauty;
    if (targets.aovs) {
        cpuAovs.resize(w, h);
        cpuTargets.aovs = &cpuAovs;
    }
    if (targets.passes) {
        cpuPasses.resize(w, h);
        if (m_scene) cpuPasses.initLightGroups(w, h, m_scene->lightGroupNames());
        cpuTargets.passes = &cpuPasses;
    }

    SampleBatchResult cpuRes{};
    double cpuMs = 1.0;
    std::thread cpuThread([&] {
        const auto t0 = std::chrono::steady_clock::now();
        cpuRes = m_cpu->render(camera, settings, cpuTargets, firstSample + gpuCount, cpuCount, cancel);
        cpuMs = std::max(0.05, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    });

    const auto gt0 = std::chrono::steady_clock::now();
    const SampleBatchResult gpuRes = m_gpu->render(camera, settings, targets, firstSample, gpuCount, cancel);
    const double gpuMs = std::max(0.05, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - gt0).count());

    cpuThread.join();

    // Sonuçları ana hedeflerde birleştir.
    if (targets.beauty) targets.beauty->merge(cpuBeauty);
    if (targets.aovs) targets.aovs->merge(cpuAovs);
    if (targets.passes) {
        if (m_scene && targets.passes->lightGroups.size() != cpuPasses.lightGroups.size()) {
            targets.passes->initLightGroups(w, h, m_scene->lightGroupNames());
        }
        targets.passes->merge(cpuPasses);
    }

    // Dinamik EMA hız dengelemesi (samples/ms oranına göre)
    if (gpuRes.completed && cpuRes.completed) {
        const double gpuRate = static_cast<double>(gpuCount) / gpuMs;
        const double cpuRate = static_cast<double>(cpuCount) / cpuMs;
        const double measuredShare = gpuRate / std::max(gpuRate + cpuRate, 1e-9);
        m_gpuShare = std::clamp(0.7 * m_gpuShare + 0.3 * measuredShare, 0.15, 0.95);
    }

    SampleBatchResult out;
    out.completed = gpuRes.completed && cpuRes.completed;
    out.samplesTraced = gpuRes.samplesTraced + cpuRes.samplesTraced;
    return out;
}

} // namespace photon
