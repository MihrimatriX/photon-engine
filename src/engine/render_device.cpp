// render_device.cpp — CPU, Vulkan GPU (Compute/RT/Wavefront) ve Hibrit cihaz kaydı ve seçimi.
#include "engine/render_device.h"
#include "engine/gpu_probe.h"
#include "engine/hybrid_device.h"
#include "engine/vulkan_device.h"

#include <cstdlib>
#include <mutex>
#include <thread>

namespace photon {

const char* deviceKindName(DeviceKind kind) {
    switch (kind) {
        case DeviceKind::Cpu: return "CPU";
        case DeviceKind::Cuda: return "CUDA";
        case DeviceKind::Optix: return "OptiX";
        case DeviceKind::Vulkan: return "Vulkan";
        case DeviceKind::D3D12: return "D3D12 DXR";
        case DeviceKind::Hybrid: return "Hibrit";
    }
    return "?";
}

namespace {

/// İşlemci cihazı: Renderer (Embree + iş parçacığı havuzu) üzerine ince bir katman.
/// Sahne paylaşılan işaretçiyle tutulur; yükleme maliyeti yoktur.
class CpuDevice final : public RenderDevice {
public:
    explicit CpuDevice(DeviceInfo info) : m_info(std::move(info)) {}

    const DeviceInfo& info() const override { return m_info; }
    DeviceFeatures features() const override {
        DeviceFeatures f;
        f.aovs = true;
        f.lightPasses = true;
        f.adaptiveSampling = true;
        return f;
    }
    void setScene(std::shared_ptr<const Scene> scene) override { m_scene = std::move(scene); }
    bool hasScene() const override { return m_scene != nullptr; }

    SampleBatchResult render(const Camera& camera, const RenderSettings& settings, RenderTargets& targets,
                             int firstSample, int count, const std::atomic<bool>* cancel) override {
        if (!m_scene || !targets.beauty) return SampleBatchResult{false, 0};
        return m_renderer.renderSamples(*m_scene, camera, settings, *targets.beauty, firstSample, count, cancel,
                                        targets.aovs, targets.passes);
    }

private:
    DeviceInfo m_info;
    std::shared_ptr<const Scene> m_scene;
    Renderer m_renderer;
};

DeviceInfo cpuInfo() {
    DeviceInfo d;
    d.id = "cpu";
    d.kind = DeviceKind::Cpu;
    d.name = cpuBrandName();
    if (d.name.empty()) d.name = "İşlemci";
    d.threads = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    d.usable = true;
#ifdef PHOTON_HAS_EMBREE
    d.note = std::to_string(d.threads) + " iş parçacığı, Embree 4";
#else
    d.note = std::to_string(d.threads) + " iş parçacığı, yerleşik BVH";
#endif
    return d;
}

int parseVulkanIndex(const std::string& id) {
    if (id.rfind("vulkan:", 0) != 0) return -1;
    return std::atoi(id.c_str() + 7);
}

} // namespace

const std::vector<DeviceInfo>& enumerateDevices() {
    static std::once_flag once;
    static std::vector<DeviceInfo> devices;
    std::call_once(once, [] {
        devices.push_back(cpuInfo());
        std::vector<DeviceInfo> vks = probeVulkanDevices();
        const DeviceInfo* bestVk = nullptr;
        for (const DeviceInfo& v : vks) {
            if (!bestVk || (isVulkanHardwareGpu(v.id) && !isVulkanHardwareGpu(bestVk->id))) bestVk = &v;
        }
        DeviceInfo hybridInfo{};
        bool hasHybrid = false;
        if (bestVk) {
            hybridInfo.id = "hybrid";
            hybridInfo.kind = DeviceKind::Hybrid;
            hybridInfo.name = bestVk->name + " + CPU";
            hybridInfo.threads = devices.front().threads;
            hybridInfo.memoryBytes = bestVk->memoryBytes;
            hybridInfo.hardwareRayTracing = bestVk->hardwareRayTracing;
            hybridInfo.usable = true;
            hybridInfo.note = "Eşzamanlı Vulkan GPU + CPU, dinamik EMA yük dengeleme";
            hasHybrid = true;
        }
        for (DeviceInfo& v : vks) devices.push_back(std::move(v));
        if (hasHybrid) devices.push_back(std::move(hybridInfo));
        for (DeviceInfo& g : probeCudaDevices()) devices.push_back(std::move(g));
    });
    return devices;
}

std::unique_ptr<RenderDevice> createDevice(const std::string& id, std::string* error) {
    const auto& devices = enumerateDevices();
    const std::string want = id.empty() ? "auto" : id;
    if (want == "auto") {
        // Donanım Vulkan GPU varsa tercih et; yazılım tarayıcıları (Lavapipe) yerine CPU kullan.
        for (const DeviceInfo& d : devices) {
            if (d.usable && d.kind == DeviceKind::Vulkan && isVulkanHardwareGpu(d.id)) {
                auto vkDev = createVulkanDevice(parseVulkanIndex(d.id), d, nullptr);
                if (vkDev) return vkDev;
            }
        }
        for (const DeviceInfo& d : devices)
            if (d.usable && d.kind == DeviceKind::Cpu) return std::make_unique<CpuDevice>(d);
    }
    for (const DeviceInfo& d : devices) {
        if (d.id != want) continue;
        if (!d.usable) {
            if (error) *error = d.name + ": " + d.note;
            return nullptr;
        }
        if (d.kind == DeviceKind::Cpu) return std::make_unique<CpuDevice>(d);
        if (d.kind == DeviceKind::Vulkan) {
            return createVulkanDevice(parseVulkanIndex(d.id), d, error);
        }
        if (d.kind == DeviceKind::Hybrid) {
            // En iyi kullanılabilir Vulkan cihazı + CPU'yu eşleştir.
            const DeviceInfo* bestVk = nullptr;
            for (const DeviceInfo& v : devices) {
                if (!v.usable || v.kind != DeviceKind::Vulkan) continue;
                if (!bestVk || (isVulkanHardwareGpu(v.id) && !isVulkanHardwareGpu(bestVk->id))) bestVk = &v;
            }
            if (!bestVk) {
                if (error) *error = "Hibrit mod için kullanılabilir Vulkan GPU bulunamadı";
                return nullptr;
            }
            auto gpu = createVulkanDevice(parseVulkanIndex(bestVk->id), *bestVk, error);
            if (!gpu) return nullptr;
            auto cpu = std::make_unique<CpuDevice>(devices.front());
            return std::make_unique<HybridDevice>(d, std::move(gpu), std::move(cpu));
        }
    }
    if (error) {
        std::string ids;
        for (const DeviceInfo& d : devices) ids += (ids.empty() ? "" : ", ") + d.id;
        *error = "Bilinmeyen cihaz: " + want + " (var olanlar: auto, " + ids + ")";
    }
    return nullptr;
}

} // namespace photon
