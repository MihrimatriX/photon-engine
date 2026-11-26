// vulkan_device.h — Evrensel Vulkan 1.1 Compute + VK_KHR_ray_query Donanım RT + Wavefront GPU arka ucu.
//
// Sıfır Vulkan SDK bağımlılığıyla çalışır (vulkan_abi.h üzerinden dinamik libvulkan.so.1 / vulkan-1.dll).
//   * Donanım RT (VK_KHR_ray_query + VK_KHR_acceleration_structure + VK_KHR_buffer_device_address):
//     TriangleMesh başına önbellekli BLAS + tek TLAS (görünürlük maskeli) + ikincil BVH2 (küre/hareket).
//   * Vulkan 1.1 Compute yedek yolu: tüm GPU'larda (GTX 1080 Pascal dahil) SAH BVH2 Megakernel.
//   * 4 aşamalı Wavefront yürütme: wf_gen → wf_intersect (RT/BVH2) → wf_shade → wf_shadow.
//   * Artımlı güncelleme (F3.8): Materials → yalnız m_matBuf; Lights → yalnız m_lightBuf/m_linkBuf;
//     Transforms → önbellekli BLAS'lar korunarak yalnız TLAS yeniden kurulur.
#pragma once

#include "engine/gpu_types.h"
#include "engine/render_device.h"
#include "engine/vulkan_abi.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace photon {

class TriangleMesh;

/// Makinedeki Vulkan fiziksel cihazlarını sorgular (her biri usable = true).
std::vector<DeviceInfo> probeVulkanDevices();

/// Verilen "vulkan:N" cihazının gerçek donanım GPU'su (Discrete/Integrated) olup olmadığını döndürür.
/// Yazılım tarayıcıları (ör. Mesa Lavapipe) açıkça seçilebilir ama "auto" modunda CPU'yu ezmez.
bool isVulkanHardwareGpu(const std::string& id);

/// Belirli bir "vulkan:N" cihazı oluşturur; başarısızsa nullptr ve @p error dolar.
std::unique_ptr<RenderDevice> createVulkanDevice(int physicalIndex, const DeviceInfo& info, std::string* error);

/// Test ve tanılama için artımlı güncelleme istatistikleri.
struct VulkanUploadStats {
    uint64_t fullUploads = 0;
    uint64_t materialUpdates = 0;
    uint64_t lightUpdates = 0;
    uint64_t transformUpdates = 0;
    uint64_t blasBuilds = 0;
    uint64_t tlasBuilds = 0;
};

class VulkanDevice final : public RenderDevice {
public:
    VulkanDevice(int physicalIndex, DeviceInfo info);
    ~VulkanDevice() override;

    bool init(std::string* error);

    const DeviceInfo& info() const override { return m_info; }
    DeviceFeatures features() const override;

    void setScene(std::shared_ptr<const Scene> scene) override;
    void updateScene(std::shared_ptr<const Scene> scene, SceneUpdateKind kind) override;
    bool hasScene() const override { return m_scene != nullptr; }

    SampleBatchResult render(const Camera& camera, const RenderSettings& settings, RenderTargets& targets,
                             int firstSample, int count, const std::atomic<bool>* cancel) override;

    VulkanUploadStats uploadStats() const { return m_stats; }
    bool hasRayQuery() const { return m_hasRayQuery; }

private:
    struct GpuBuffer {
        vk::VkBuffer buf = vk::VK_NULL_HANDLE;
        vk::VkDeviceMemory mem = vk::VK_NULL_HANDLE;
        vk::VkDeviceSize size = 0;
        void* mapped = nullptr;
        vk::VkDeviceAddress addr = 0;
    };

    struct CachedBlas {
        vk::VkAccelerationStructureKHR handle = vk::VK_NULL_HANDLE;
        GpuBuffer asBuf;
        GpuBuffer vertBuf;
        GpuBuffer idxBuf;
        vk::VkDeviceAddress deviceAddress = 0;
        uint32_t triCount = 0;
    };

    struct StaticTlasEntry {
        const TriangleMesh* meshKey = nullptr; ///< nullptr ise bağımsız üçgen grubu
        uint32_t primBase = 0;
        uint32_t triCount = 0;
        uint8_t visibilityMask = VisAll;
        float transform3x4[3][4] = {};
        std::vector<float> standaloneVerts;    ///< meshKey == nullptr iken dünya üçgen köşeleri
    };

    struct MaterialSlotMap {
        std::vector<const Material*> ordered;
        std::unordered_map<const Material*, uint32_t> indexOf;
    };

    bool createBuffer(vk::VkDeviceSize size, vk::VkFlags usage, GpuBuffer& out, bool deviceAddress = false);
    void destroyBuffer(GpuBuffer& b);
    bool uploadBuffer(const void* data, vk::VkDeviceSize bytes, vk::VkFlags usage, GpuBuffer& out,
                      bool deviceAddress = false);
    uint32_t findMemoryType(uint32_t typeBits, vk::VkFlags props) const;
    bool createPipelines(std::string* error);
    void destroyPipelines();
    void destroyTlas();
    void destroyAllBlas();

    void rebuildMaterialsAndTextures();
    void rebuildLightsAndLinks();
    void rebuildGeometryAndBvh(bool keepBlasCache);
    bool buildOrReuseBlasForMesh(const TriangleMesh* mesh, CachedBlas& out);
    bool buildBlasForStandalone(const std::vector<float>& verts, CachedBlas& out);
    bool rebuildTlas(const std::vector<StaticTlasEntry>& entries);

    bool ensureOutputBuffers(int w, int h, bool wantAovs, bool wantPasses, bool wantWavefront);
    void updateDescriptorSets(bool wantWavefront);
    bool submitAndWait(vk::VkCommandBuffer cmd);

    int m_physIndex = 0;
    DeviceInfo m_info;
    vk::VulkanApi m_vk{};
    vk::VkInstance m_instance = nullptr;
    vk::VkPhysicalDevice m_phys = nullptr;
    vk::VkDevice m_device = nullptr;
    vk::VkQueue m_queue = nullptr;
    uint32_t m_queueFamily = 0;
    vk::VkPhysicalDeviceMemoryProperties m_memProps{};
    vk::VkCommandPool m_cmdPool = vk::VK_NULL_HANDLE;
    vk::VkCommandBuffer m_cmdBuf = nullptr;
    vk::VkFence m_fence = vk::VK_NULL_HANDLE;
    bool m_hasRayQuery = false;

    // Boru hatları ve tanımlayıcı kümeleri
    vk::VkDescriptorPool m_descPool = vk::VK_NULL_HANDLE;
    vk::VkDescriptorSetLayout m_dslCompute = vk::VK_NULL_HANDLE;
    vk::VkDescriptorSetLayout m_dslRt = vk::VK_NULL_HANDLE;
    vk::VkDescriptorSetLayout m_dslWf = vk::VK_NULL_HANDLE;
    vk::VkDescriptorSetLayout m_dslWfRt = vk::VK_NULL_HANDLE;
    vk::VkDescriptorSet m_dsCompute = vk::VK_NULL_HANDLE;
    vk::VkDescriptorSet m_dsRt = vk::VK_NULL_HANDLE;
    vk::VkDescriptorSet m_dsWf = vk::VK_NULL_HANDLE;
    vk::VkDescriptorSet m_dsWfRt = vk::VK_NULL_HANDLE;
    vk::VkPipelineLayout m_plCompute = vk::VK_NULL_HANDLE;
    vk::VkPipelineLayout m_plRt = vk::VK_NULL_HANDLE;
    vk::VkPipelineLayout m_plWf = vk::VK_NULL_HANDLE;
    vk::VkPipelineLayout m_plWfRt = vk::VK_NULL_HANDLE;
    vk::VkPipeline m_pipeCompute = vk::VK_NULL_HANDLE;
    vk::VkPipeline m_pipeRt = vk::VK_NULL_HANDLE;
    vk::VkPipeline m_pipeWfGen = vk::VK_NULL_HANDLE;
    vk::VkPipeline m_pipeWfIntersect = vk::VK_NULL_HANDLE;
    vk::VkPipeline m_pipeWfIntersectRt = vk::VK_NULL_HANDLE;
    vk::VkPipeline m_pipeWfShade = vk::VK_NULL_HANDLE;
    vk::VkPipeline m_pipeWfShadow = vk::VK_NULL_HANDLE;

    // Sahne tamponları
    std::shared_ptr<const Scene> m_scene;
    MaterialSlotMap m_matMap;
    std::unordered_map<uint64_t, uint32_t> m_uidToLinkSlot;
    GpuBuffer m_uboBuf;
    GpuBuffer m_primBuf;
    GpuBuffer m_bvhBuf;
    GpuBuffer m_matBuf;
    GpuBuffer m_lightBuf;
    GpuBuffer m_texInfoBuf;
    GpuBuffer m_texPixBuf;
    GpuBuffer m_envPixBuf;
    GpuBuffer m_linkBuf;
    GpuBuffer m_motionBuf;

    // Çıktı ve Wavefront tamponları
    GpuBuffer m_outBeautyBuf;
    GpuBuffer m_outAovBuf;
    GpuBuffer m_outPassBuf;
    GpuBuffer m_wfPathBuf;
    GpuBuffer m_wfRayABuf;
    GpuBuffer m_wfRayBBuf;
    GpuBuffer m_wfHitBuf;
    GpuBuffer m_wfShadowBuf;
    GpuBuffer m_wfCntBuf;
    int m_outW = 0;
    int m_outH = 0;
    bool m_outHasAovs = false;
    bool m_outHasPasses = false;
    bool m_outHasWf = false;

    // Donanım RT BLAS önbelleği + TLAS
    std::unordered_map<const TriangleMesh*, CachedBlas> m_blasCache;
    std::unordered_map<uint64_t, CachedBlas> m_standaloneBlasCache;
    vk::VkAccelerationStructureKHR m_tlas = vk::VK_NULL_HANDLE;
    GpuBuffer m_tlasBuf;
    GpuBuffer m_tlasInstBuf;

    // Sahne meta sayıları
    uint32_t m_primCount = 0;
    uint32_t m_bvhNodeCount = 0;
    uint32_t m_secBvhRoot = 0;
    bool m_hasSecBvh = false;
    uint32_t m_lightCount = 0;
    uint32_t m_envW = 0;
    uint32_t m_envH = 0;
    bool m_hasEnv = false;
    bool m_descriptorsDirty = true;
    VulkanUploadStats m_stats{};
};

} // namespace photon
