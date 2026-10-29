// render_device.cpp — CPU cihazı ve cihaz listesi / seçimi.
#include "engine/render_device.h"
#include "engine/gpu_probe.h"

#include <mutex>
#include <thread>

namespace photon {

const char* deviceKindName(DeviceKind kind) {
    switch (kind) {
        case DeviceKind::Cpu: return "CPU";
        case DeviceKind::Cuda: return "CUDA";
        case DeviceKind::Optix: return "OptiX";
        case DeviceKind::Vulkan: return "Vulkan RT";
        case DeviceKind::D3D12: return "D3D12 DXR";
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

} // namespace

const std::vector<DeviceInfo>& enumerateDevices() {
    static std::once_flag once;
    static std::vector<DeviceInfo> devices;
    std::call_once(once, [] {
        devices.push_back(cpuInfo());
        for (DeviceInfo& g : probeCudaDevices()) devices.push_back(std::move(g));
    });
    return devices;
}

std::unique_ptr<RenderDevice> createDevice(const std::string& id, std::string* error) {
    const auto& devices = enumerateDevices();
    const std::string want = id.empty() ? "auto" : id;
    if (want == "auto") {
        // En iyi kullanılabilir cihaz. GPU arka uçları geldiğinde burada GPU'ya öncelik verilir.
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
    }
    if (error) {
        std::string ids;
        for (const DeviceInfo& d : devices) ids += (ids.empty() ? "" : ", ") + d.id;
        *error = "Bilinmeyen cihaz: " + want + " (var olanlar: auto, " + ids + ")";
    }
    return nullptr;
}

} // namespace photon
