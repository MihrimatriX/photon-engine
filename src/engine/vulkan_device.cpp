// vulkan_device.cpp — Sıfır SDK dinamik Vulkan yükleyici + Vulkan 1.1 Compute / VK_KHR_ray_query
// Donanım RT / 4 aşamalı Wavefront GPU yol izleyici arka ucu.
#include "engine/vulkan_device.h"

#include "core/image/cryptomatte.h"
#include "core/math/constants.h"
#include "core/math/quat.h"
#include "geometry/mesh.h"
#include "geometry/mesh_instance.h"
#include "geometry/sphere.h"
#include "geometry/triangle.h"
#include "lights/area_light.h"
#include "lights/directional_light.h"
#include "lights/environment_light.h"
#include "lights/point_light.h"
#include "lights/spot_light.h"
#include "materials/dielectric.h"
#include "materials/disney.h"
#include "materials/lambertian.h"
#include "materials/mirror.h"

// Depoda izlenen önceden derlenmiş SPIR-V başlıkları (sıfır Vulkan SDK bağımlılığı).
#include "engine/shaders/pathtracer_rt_spv.h"
#include "engine/shaders/pathtracer_spv.h"
#include "engine/shaders/pathtracer_wf_spv.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <numeric>
#include <sstream>
#include <unordered_set>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace photon {

// ═══════════════════════════════════════════════════════════════════════════
// Dinamik Vulkan yükleyici (vulkan_abi.h)
// ═══════════════════════════════════════════════════════════════════════════
namespace vk {

namespace {

void* openVulkanLib() {
#if defined(_WIN32)
    return static_cast<void*>(LoadLibraryExA("vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32));
#else
    void* h = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!h) h = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    return h;
#endif
}

template <typename F>
F libSymbol(void* h, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(h), name)));
#else
    return reinterpret_cast<F>(dlsym(h, name));
#endif
}

template <typename F>
F instProc(PFN_vkGetInstanceProcAddr gipa, VkInstance inst, const char* name) {
    return reinterpret_cast<F>(gipa(inst, name));
}

template <typename F>
F devProc(PFN_vkGetDeviceProcAddr gdpa, VkDevice dev, const char* name) {
    return reinterpret_cast<F>(gdpa(dev, name));
}

} // namespace

const VulkanApi& loadVulkanGlobal() {
    static std::once_flag once;
    static VulkanApi api{};
    std::call_once(once, [] {
        void* h = openVulkanLib();
        if (!h) return;
        api.getInstanceProcAddr = libSymbol<PFN_vkGetInstanceProcAddr>(h, "vkGetInstanceProcAddr");
        if (!api.getInstanceProcAddr) return;
        api.enumerateInstanceVersion = instProc<PFN_vkEnumerateInstanceVersion>(api.getInstanceProcAddr, nullptr, "vkEnumerateInstanceVersion");
        api.createInstance = instProc<PFN_vkCreateInstance>(api.getInstanceProcAddr, nullptr, "vkCreateInstance");
        api.loaded = (api.createInstance != nullptr);
    });
    return api;
}

bool loadVulkanInstance(VkInstance instance, VulkanApi& api) {
    if (!api.getInstanceProcAddr || !instance) return false;
#define VK_INST_FN(field, name) api.field = instProc<PFN_##name>(api.getInstanceProcAddr, instance, #name)
    VK_INST_FN(destroyInstance, vkDestroyInstance);
    VK_INST_FN(enumeratePhysicalDevices, vkEnumeratePhysicalDevices);
    VK_INST_FN(getPhysicalDeviceProperties, vkGetPhysicalDeviceProperties);
    VK_INST_FN(getPhysicalDeviceMemoryProperties, vkGetPhysicalDeviceMemoryProperties);
    VK_INST_FN(getPhysicalDeviceQueueFamilyProperties, vkGetPhysicalDeviceQueueFamilyProperties);
    VK_INST_FN(enumerateDeviceExtensionProperties, vkEnumerateDeviceExtensionProperties);
    VK_INST_FN(getPhysicalDeviceFeatures2, vkGetPhysicalDeviceFeatures2);
    VK_INST_FN(createDevice, vkCreateDevice);
    VK_INST_FN(getDeviceProcAddr, vkGetDeviceProcAddr);
#undef VK_INST_FN
    return api.destroyInstance && api.enumeratePhysicalDevices && api.getPhysicalDeviceProperties &&
           api.getPhysicalDeviceMemoryProperties && api.getPhysicalDeviceQueueFamilyProperties &&
           api.enumerateDeviceExtensionProperties && api.createDevice && api.getDeviceProcAddr;
}

bool loadVulkanDevice(VkDevice device, VulkanApi& api, bool loadRayQuery) {
    if (!api.getDeviceProcAddr || !device) return false;
#define VK_DEV_FN(field, name) api.field = devProc<PFN_##name>(api.getDeviceProcAddr, device, #name)
    VK_DEV_FN(destroyDevice, vkDestroyDevice);
    VK_DEV_FN(getDeviceQueue, vkGetDeviceQueue);
    VK_DEV_FN(deviceWaitIdle, vkDeviceWaitIdle);
    VK_DEV_FN(createBuffer, vkCreateBuffer);
    VK_DEV_FN(destroyBuffer, vkDestroyBuffer);
    VK_DEV_FN(getBufferMemoryRequirements, vkGetBufferMemoryRequirements);
    VK_DEV_FN(allocateMemory, vkAllocateMemory);
    VK_DEV_FN(freeMemory, vkFreeMemory);
    VK_DEV_FN(bindBufferMemory, vkBindBufferMemory);
    VK_DEV_FN(mapMemory, vkMapMemory);
    VK_DEV_FN(unmapMemory, vkUnmapMemory);
    VK_DEV_FN(createShaderModule, vkCreateShaderModule);
    VK_DEV_FN(destroyShaderModule, vkDestroyShaderModule);
    VK_DEV_FN(createDescriptorSetLayout, vkCreateDescriptorSetLayout);
    VK_DEV_FN(destroyDescriptorSetLayout, vkDestroyDescriptorSetLayout);
    VK_DEV_FN(createPipelineLayout, vkCreatePipelineLayout);
    VK_DEV_FN(destroyPipelineLayout, vkDestroyPipelineLayout);
    VK_DEV_FN(createComputePipelines, vkCreateComputePipelines);
    VK_DEV_FN(destroyPipeline, vkDestroyPipeline);
    VK_DEV_FN(createDescriptorPool, vkCreateDescriptorPool);
    VK_DEV_FN(destroyDescriptorPool, vkDestroyDescriptorPool);
    VK_DEV_FN(allocateDescriptorSets, vkAllocateDescriptorSets);
    VK_DEV_FN(updateDescriptorSets, vkUpdateDescriptorSets);
    VK_DEV_FN(createCommandPool, vkCreateCommandPool);
    VK_DEV_FN(destroyCommandPool, vkDestroyCommandPool);
    VK_DEV_FN(allocateCommandBuffers, vkAllocateCommandBuffers);
    VK_DEV_FN(freeCommandBuffers, vkFreeCommandBuffers);
    VK_DEV_FN(resetCommandBuffer, vkResetCommandBuffer);
    VK_DEV_FN(beginCommandBuffer, vkBeginCommandBuffer);
    VK_DEV_FN(endCommandBuffer, vkEndCommandBuffer);
    VK_DEV_FN(cmdBindPipeline, vkCmdBindPipeline);
    VK_DEV_FN(cmdBindDescriptorSets, vkCmdBindDescriptorSets);
    VK_DEV_FN(cmdPushConstants, vkCmdPushConstants);
    VK_DEV_FN(cmdDispatch, vkCmdDispatch);
    VK_DEV_FN(cmdCopyBuffer, vkCmdCopyBuffer);
    VK_DEV_FN(cmdFillBuffer, vkCmdFillBuffer);
    VK_DEV_FN(cmdPipelineBarrier, vkCmdPipelineBarrier);
    VK_DEV_FN(queueSubmit, vkQueueSubmit);
    VK_DEV_FN(createFence, vkCreateFence);
    VK_DEV_FN(destroyFence, vkDestroyFence);
    VK_DEV_FN(waitForFences, vkWaitForFences);
    VK_DEV_FN(resetFences, vkResetFences);
#undef VK_DEV_FN

    bool ok = api.destroyDevice && api.getDeviceQueue && api.createBuffer && api.allocateMemory &&
              api.bindBufferMemory && api.mapMemory && api.createShaderModule && api.createComputePipelines &&
              api.createDescriptorSetLayout && api.createDescriptorPool && api.allocateDescriptorSets &&
              api.updateDescriptorSets && api.createCommandPool && api.allocateCommandBuffers &&
              api.cmdBindPipeline && api.cmdDispatch && api.cmdFillBuffer && api.cmdPipelineBarrier &&
              api.queueSubmit && api.createFence && api.waitForFences;
    if (!ok) return false;

    if (loadRayQuery) {
        api.getBufferDeviceAddress = devProc<PFN_vkGetBufferDeviceAddress>(api.getDeviceProcAddr, device, "vkGetBufferDeviceAddress");
        if (!api.getBufferDeviceAddress)
            api.getBufferDeviceAddress = devProc<PFN_vkGetBufferDeviceAddress>(api.getDeviceProcAddr, device, "vkGetBufferDeviceAddressKHR");
        api.createAccelerationStructureKHR = devProc<PFN_vkCreateAccelerationStructureKHR>(api.getDeviceProcAddr, device, "vkCreateAccelerationStructureKHR");
        api.destroyAccelerationStructureKHR = devProc<PFN_vkDestroyAccelerationStructureKHR>(api.getDeviceProcAddr, device, "vkDestroyAccelerationStructureKHR");
        api.getAccelerationStructureBuildSizesKHR = devProc<PFN_vkGetAccelerationStructureBuildSizesKHR>(api.getDeviceProcAddr, device, "vkGetAccelerationStructureBuildSizesKHR");
        api.getAccelerationStructureDeviceAddressKHR = devProc<PFN_vkGetAccelerationStructureDeviceAddressKHR>(api.getDeviceProcAddr, device, "vkGetAccelerationStructureDeviceAddressKHR");
        api.cmdBuildAccelerationStructuresKHR = devProc<PFN_vkCmdBuildAccelerationStructuresKHR>(api.getDeviceProcAddr, device, "vkCmdBuildAccelerationStructuresKHR");
    }
    return true;
}

} // namespace vk

// ═══════════════════════════════════════════════════════════════════════════
// Yardımcılar: bit paketleme, SAH BVH2 kurucu, RT yetenek sorgusu
// ═══════════════════════════════════════════════════════════════════════════
namespace {

float u32ToF32(uint32_t u) {
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof(float));
    return f;
}

float i32ToF32(int32_t i) {
    float f = 0.0f;
    std::memcpy(&f, &i, sizeof(float));
    return f;
}

uint32_t f32ToU32(float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(uint32_t));
    return u;
}

gpu::Vec4 makeV4(const Vec3f& v, float w = 0.0f) { return gpu::Vec4{v.x, v.y, v.z, w}; }
gpu::Vec4 makeC4(const Color3f& c, float w = 0.0f) { return gpu::Vec4{c.r, c.g, c.b, w}; }

void mat4To3x4(const Mat4f& m, float out[3][4]) {
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c) out[r][c] = m.data[r][c];
}

void identity3x4(float out[3][4]) {
    std::memset(out, 0, sizeof(float) * 12);
    out[0][0] = out[1][1] = out[2][2] = 1.0f;
}

bool queryRayQuerySupport(const vk::VulkanApi& api, vk::VkPhysicalDevice phys,
                          const vk::VkPhysicalDeviceProperties& props) {
    if (props.apiVersion < vk::VK_API_VERSION_1_2 || !api.getPhysicalDeviceFeatures2) return false;
    uint32_t extCount = 0;
    if (api.enumerateDeviceExtensionProperties(phys, nullptr, &extCount, nullptr) != vk::VK_SUCCESS || extCount == 0)
        return false;
    std::vector<vk::VkExtensionProperties> exts(extCount);
    if (api.enumerateDeviceExtensionProperties(phys, nullptr, &extCount, exts.data()) != vk::VK_SUCCESS) return false;
    auto hasExt = [&](const char* name) {
        for (const auto& e : exts)
            if (std::strcmp(e.extensionName, name) == 0) return true;
        return false;
    };
    if (!hasExt("VK_KHR_acceleration_structure") || !hasExt("VK_KHR_deferred_host_operations") ||
        !hasExt("VK_KHR_ray_query"))
        return false;

    vk::VkPhysicalDeviceRayQueryFeaturesKHR rq{};
    vk::VkPhysicalDeviceAccelerationStructureFeaturesKHR as{};
    vk::VkPhysicalDeviceBufferDeviceAddressFeatures bda{};
    vk::VkPhysicalDeviceFeatures2 f2{};
    f2.pNext = &bda;
    bda.pNext = &as;
    as.pNext = &rq;
    api.getPhysicalDeviceFeatures2(phys, &f2);
    return bda.bufferDeviceAddress == vk::VK_TRUE && as.accelerationStructure == vk::VK_TRUE &&
           rq.rayQuery == vk::VK_TRUE;
}

uint32_t selectComputeQueueFamily(const vk::VulkanApi& api, vk::VkPhysicalDevice phys) {
    uint32_t count = 0;
    api.getPhysicalDeviceQueueFamilyProperties(phys, &count, nullptr);
    std::vector<vk::VkQueueFamilyProperties> fams(count);
    api.getPhysicalDeviceQueueFamilyProperties(phys, &count, fams.data());
    for (uint32_t i = 0; i < count; ++i) {
        if ((fams[i].queueFlags & vk::VK_QUEUE_COMPUTE_BIT) != 0u) return i;
    }
    return 0u;
}

vk::VkInstance createVulkanInstance(const vk::VulkanApi& globalApi, vk::VulkanApi& outApi, uint32_t* chosenApi = nullptr) {
    if (!globalApi.loaded) return nullptr;
    uint32_t instVer = vk::VK_API_VERSION_1_1;
    if (globalApi.enumerateInstanceVersion) {
        uint32_t v = 0;
        if (globalApi.enumerateInstanceVersion(&v) == vk::VK_SUCCESS && v >= vk::VK_API_VERSION_1_2)
            instVer = vk::VK_API_VERSION_1_2;
    }
    if (chosenApi) *chosenApi = instVer;
    vk::VkApplicationInfo app{};
    app.pApplicationName = "PhotonEngine";
    app.applicationVersion = 5;
    app.pEngineName = "PhotonEngine";
    app.engineVersion = 5;
    app.apiVersion = instVer;

    vk::VkInstanceCreateInfo ci{};
    ci.pApplicationInfo = &app;
    vk::VkInstance inst = nullptr;
    if (globalApi.createInstance(&ci, nullptr, &inst) != vk::VK_SUCCESS || !inst) return nullptr;
    outApi = globalApi;
    if (!vk::loadVulkanInstance(inst, outApi)) {
        if (outApi.destroyInstance) outApi.destroyInstance(inst, nullptr);
        return nullptr;
    }
    return inst;
}

// 1-ilkel/yaprak SAH BVH2 kurucu: prims[] sırası değişmez (RT customIndex+primIndex ile tam uyumlu).
uint32_t buildSahBvh2(const std::vector<AABB>& primBounds, std::vector<uint32_t>& indices,
                      size_t begin, size_t end, std::vector<gpu::GpuBvhNode>& nodes) {
    const uint32_t nodeIdx = static_cast<uint32_t>(nodes.size());
    nodes.emplace_back();

    AABB box = AABB::empty();
    AABB centroidBox = AABB::empty();
    for (size_t i = begin; i < end; ++i) {
        const AABB& b = primBounds[indices[i]];
        box.merge(b);
        centroidBox.merge(b.centroid());
    }
    // Sıfır kalınlıklı kutulara küçük pay ver (düzlem üçgenler)
    box.pMin -= Vec3f(1e-5f);
    box.pMax += Vec3f(1e-5f);

    const size_t count = end - begin;
    if (count <= 1) {
        nodes[nodeIdx].bmin_left = makeV4(box.pMin, u32ToF32(count == 1 ? indices[begin] : 0u));
        nodes[nodeIdx].bmax_meta = makeV4(box.pMax, u32ToF32(0x80000000u | static_cast<uint32_t>(count)));
        return nodeIdx;
    }

    // 8 kutulu SAH bölme
    int bestAxis = -1;
    int bestSplit = -1;
    float bestCost = INFINITY_F;
    const Vec3f cExt = centroidBox.pMax - centroidBox.pMin;
    constexpr int kBins = 8;

    for (int axis = 0; axis < 3; ++axis) {
        const float ext = axis == 0 ? cExt.x : (axis == 1 ? cExt.y : cExt.z);
        if (ext <= 1e-7f) continue;
        const float cMin = axis == 0 ? centroidBox.pMin.x : (axis == 1 ? centroidBox.pMin.y : centroidBox.pMin.z);
        AABB binBox[kBins];
        int binCnt[kBins] = {};
        for (int b = 0; b < kBins; ++b) binBox[b] = AABB::empty();
        for (size_t i = begin; i < end; ++i) {
            const Vec3f c = primBounds[indices[i]].centroid();
            const float v = axis == 0 ? c.x : (axis == 1 ? c.y : c.z);
            int bi = static_cast<int>(((v - cMin) / ext) * static_cast<float>(kBins));
            bi = std::clamp(bi, 0, kBins - 1);
            binBox[bi].merge(primBounds[indices[i]]);
            ++binCnt[bi];
        }
        AABB leftBox[kBins - 1], rightBox[kBins - 1];
        int leftCnt[kBins - 1], rightCnt[kBins - 1];
        AABB accL = AABB::empty();
        int cntL = 0;
        for (int b = 0; b < kBins - 1; ++b) {
            if (binCnt[b] > 0) accL.merge(binBox[b]);
            cntL += binCnt[b];
            leftBox[b] = accL;
            leftCnt[b] = cntL;
        }
        AABB accR = AABB::empty();
        int cntR = 0;
        for (int b = kBins - 1; b > 0; --b) {
            if (binCnt[b] > 0) accR.merge(binBox[b]);
            cntR += binCnt[b];
            rightBox[b - 1] = accR;
            rightCnt[b - 1] = cntR;
        }
        for (int b = 0; b < kBins - 1; ++b) {
            if (leftCnt[b] == 0 || rightCnt[b] == 0) continue;
            const float cost = leftBox[b].surfaceArea() * static_cast<float>(leftCnt[b]) +
                               rightBox[b].surfaceArea() * static_cast<float>(rightCnt[b]);
            if (cost < bestCost) {
                bestCost = cost;
                bestAxis = axis;
                bestSplit = b;
            }
        }
    }

    size_t mid = begin + count / 2;
    if (bestAxis >= 0) {
        const float ext = bestAxis == 0 ? cExt.x : (bestAxis == 1 ? cExt.y : cExt.z);
        const float cMin = bestAxis == 0 ? centroidBox.pMin.x : (bestAxis == 1 ? centroidBox.pMin.y : centroidBox.pMin.z);
        auto it = std::partition(indices.begin() + static_cast<ptrdiff_t>(begin),
                                 indices.begin() + static_cast<ptrdiff_t>(end), [&](uint32_t pi) {
            const Vec3f c = primBounds[pi].centroid();
            const float v = bestAxis == 0 ? c.x : (bestAxis == 1 ? c.y : c.z);
            int bi = std::clamp(static_cast<int>(((v - cMin) / ext) * static_cast<float>(kBins)), 0, kBins - 1);
            return bi <= bestSplit;
        });
        mid = static_cast<size_t>(it - indices.begin());
        if (mid == begin || mid == end) mid = begin + count / 2;
    }

    const uint32_t left = buildSahBvh2(primBounds, indices, begin, mid, nodes);
    const uint32_t right = buildSahBvh2(primBounds, indices, mid, end, nodes);
    nodes[nodeIdx].bmin_left = makeV4(box.pMin, u32ToF32(left));
    nodes[nodeIdx].bmax_meta = makeV4(box.pMax, u32ToF32(right));
    return nodeIdx;
}

std::mutex gHwMutex;
std::unordered_set<std::string> gHardwareVulkanIds;

void setBit64(uint32_t& lo, uint32_t& hi, uint32_t slot) {
    if (slot >= 64u) return;
    if (slot < 32u) lo |= (1u << slot);
    else hi |= (1u << (slot - 32u));
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Cihaz tarama ve fabrika
// ═══════════════════════════════════════════════════════════════════════════

std::vector<DeviceInfo> probeVulkanDevices() {
    std::vector<DeviceInfo> out;
    const vk::VulkanApi& globalApi = vk::loadVulkanGlobal();
    if (!globalApi.loaded) return out;
    vk::VulkanApi api{};
    vk::VkInstance inst = createVulkanInstance(globalApi, api);
    if (!inst) return out;

    uint32_t count = 0;
    if (api.enumeratePhysicalDevices(inst, &count, nullptr) == vk::VK_SUCCESS && count > 0) {
        std::vector<vk::VkPhysicalDevice> phys(count);
        if (api.enumeratePhysicalDevices(inst, &count, phys.data()) == vk::VK_SUCCESS) {
            std::lock_guard<std::mutex> lk(gHwMutex);
            gHardwareVulkanIds.clear();
            for (uint32_t i = 0; i < count; ++i) {
                vk::VkPhysicalDeviceProperties props{};
                api.getPhysicalDeviceProperties(phys[i], &props);
                vk::VkPhysicalDeviceMemoryProperties mem{};
                api.getPhysicalDeviceMemoryProperties(phys[i], &mem);
                uint64_t vram = 0;
                for (uint32_t h = 0; h < mem.memoryHeapCount; ++h) {
                    if ((mem.memoryHeaps[h].flags & vk::VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0u)
                        vram += mem.memoryHeaps[h].size;
                }
                const bool rt = queryRayQuerySupport(api, phys[i], props);
                const bool isHw = props.deviceType == vk::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
                                  props.deviceType == vk::VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;

                DeviceInfo d;
                d.id = "vulkan:" + std::to_string(i);
                d.kind = DeviceKind::Vulkan;
                d.name = props.deviceName;
                d.memoryBytes = vram;
                d.computeMajor = static_cast<int>(vk::apiVersionMajor(props.apiVersion));
                d.computeMinor = static_cast<int>(vk::apiVersionMinor(props.apiVersion));
                d.hardwareRayTracing = rt;
                d.usable = true;
                std::ostringstream note;
                note << "Vulkan " << d.computeMajor << "." << d.computeMinor;
                if (rt) note << ", Donanım RT (VK_KHR_ray_query) + Wavefront";
                else note << ", Compute SAH BVH2 + Wavefront";
                if (props.deviceType == vk::VK_PHYSICAL_DEVICE_TYPE_CPU) note << " (yazılım sürücü)";
                d.note = note.str();
                if (isHw) gHardwareVulkanIds.insert(d.id);
                out.push_back(std::move(d));
            }
        }
    }
    api.destroyInstance(inst, nullptr);
    return out;
}

bool isVulkanHardwareGpu(const std::string& id) {
    std::lock_guard<std::mutex> lk(gHwMutex);
    return gHardwareVulkanIds.count(id) > 0;
}

std::unique_ptr<RenderDevice> createVulkanDevice(int physicalIndex, const DeviceInfo& info, std::string* error) {
    auto dev = std::make_unique<VulkanDevice>(physicalIndex, info);
    if (!dev->init(error)) return nullptr;
    return dev;
}

// ═══════════════════════════════════════════════════════════════════════════
// VulkanDevice yaşam döngüsü
// ═══════════════════════════════════════════════════════════════════════════

VulkanDevice::VulkanDevice(int physicalIndex, DeviceInfo info)
    : m_physIndex(physicalIndex), m_info(std::move(info)) {}

VulkanDevice::~VulkanDevice() {
    if (m_device && m_vk.deviceWaitIdle) m_vk.deviceWaitIdle(m_device);
    destroyTlas();
    destroyAllBlas();
    destroyPipelines();
    auto freeBuf = [&](GpuBuffer& b) { destroyBuffer(b); };
    freeBuf(m_uboBuf);
    freeBuf(m_primBuf);
    freeBuf(m_bvhBuf);
    freeBuf(m_matBuf);
    freeBuf(m_lightBuf);
    freeBuf(m_texInfoBuf);
    freeBuf(m_texPixBuf);
    freeBuf(m_envPixBuf);
    freeBuf(m_linkBuf);
    freeBuf(m_motionBuf);
    freeBuf(m_outBeautyBuf);
    freeBuf(m_outAovBuf);
    freeBuf(m_outPassBuf);
    freeBuf(m_wfPathBuf);
    freeBuf(m_wfRayABuf);
    freeBuf(m_wfRayBBuf);
    freeBuf(m_wfHitBuf);
    freeBuf(m_wfShadowBuf);
    freeBuf(m_wfCntBuf);
    if (m_fence && m_vk.destroyFence) m_vk.destroyFence(m_device, m_fence, nullptr);
    if (m_cmdPool && m_vk.destroyCommandPool) m_vk.destroyCommandPool(m_device, m_cmdPool, nullptr);
    if (m_device && m_vk.destroyDevice) m_vk.destroyDevice(m_device, nullptr);
    if (m_instance && m_vk.destroyInstance) m_vk.destroyInstance(m_instance, nullptr);
}

DeviceFeatures VulkanDevice::features() const {
    DeviceFeatures f;
    f.aovs = true;
    f.lightPasses = true;
    f.adaptiveSampling = false;
    return f;
}

bool VulkanDevice::init(std::string* error) {
    const vk::VulkanApi& globalApi = vk::loadVulkanGlobal();
    if (!globalApi.loaded) {
        if (error) *error = "Vulkan sürücüsü (libvulkan / vulkan-1.dll) bulunamadı";
        return false;
    }
    m_instance = createVulkanInstance(globalApi, m_vk);
    if (!m_instance) {
        if (error) *error = "VkInstance oluşturulamadı";
        return false;
    }

    uint32_t count = 0;
    if (m_vk.enumeratePhysicalDevices(m_instance, &count, nullptr) != vk::VK_SUCCESS ||
        m_physIndex < 0 || static_cast<uint32_t>(m_physIndex) >= count) {
        if (error) *error = "Vulkan fiziksel cihazı bulunamadı";
        return false;
    }
    std::vector<vk::VkPhysicalDevice> phys(count);
    m_vk.enumeratePhysicalDevices(m_instance, &count, phys.data());
    m_phys = phys[static_cast<size_t>(m_physIndex)];

    vk::VkPhysicalDeviceProperties props{};
    m_vk.getPhysicalDeviceProperties(m_phys, &props);
    m_vk.getPhysicalDeviceMemoryProperties(m_phys, &m_memProps);
    m_queueFamily = selectComputeQueueFamily(m_vk, m_phys);
    m_hasRayQuery = queryRayQuerySupport(m_vk, m_phys, props);

    float prio = 1.0f;
    vk::VkDeviceQueueCreateInfo qci{};
    qci.queueFamilyIndex = m_queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    std::vector<const char*> exts;
    vk::VkPhysicalDeviceBufferDeviceAddressFeatures bda{};
    vk::VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeat{};
    vk::VkPhysicalDeviceRayQueryFeaturesKHR rqFeat{};

    vk::VkDeviceCreateInfo dci{};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;

    if (m_hasRayQuery) {
        exts = {
            "VK_KHR_acceleration_structure",
            "VK_KHR_deferred_host_operations",
            "VK_KHR_ray_query",
        };
        bda.bufferDeviceAddress = vk::VK_TRUE;
        asFeat.accelerationStructure = vk::VK_TRUE;
        rqFeat.rayQuery = vk::VK_TRUE;
        bda.pNext = &asFeat;
        asFeat.pNext = &rqFeat;
        dci.pNext = &bda;
        dci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
        dci.ppEnabledExtensionNames = exts.data();
    }

    if (m_vk.createDevice(m_phys, &dci, nullptr, &m_device) != vk::VK_SUCCESS || !m_device) {
        // RT uzantılarıyla açılmazsa düz Vulkan 1.1 Compute'a düş
        if (m_hasRayQuery) {
            m_hasRayQuery = false;
            m_info.hardwareRayTracing = false;
            dci.pNext = nullptr;
            dci.enabledExtensionCount = 0;
            dci.ppEnabledExtensionNames = nullptr;
            if (m_vk.createDevice(m_phys, &dci, nullptr, &m_device) != vk::VK_SUCCESS || !m_device) {
                if (error) *error = "VkDevice oluşturulamadı";
                return false;
            }
        } else {
            if (error) *error = "VkDevice oluşturulamadı";
            return false;
        }
    }

    if (!vk::loadVulkanDevice(m_device, m_vk, m_hasRayQuery)) {
        if (error) *error = "Vulkan cihaz işlevleri yüklenemedi";
        return false;
    }
    if (m_hasRayQuery && (!m_vk.getBufferDeviceAddress || !m_vk.createAccelerationStructureKHR ||
                          !m_vk.getAccelerationStructureBuildSizesKHR ||
                          !m_vk.getAccelerationStructureDeviceAddressKHR || !m_vk.cmdBuildAccelerationStructuresKHR)) {
        m_hasRayQuery = false;
        m_info.hardwareRayTracing = false;
    }

    m_vk.getDeviceQueue(m_device, m_queueFamily, 0, &m_queue);

    vk::VkCommandPoolCreateInfo cpci{};
    cpci.flags = vk::VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = m_queueFamily;
    if (m_vk.createCommandPool(m_device, &cpci, nullptr, &m_cmdPool) != vk::VK_SUCCESS) {
        if (error) *error = "VkCommandPool oluşturulamadı";
        return false;
    }

    vk::VkCommandBufferAllocateInfo cbai{};
    cbai.commandPool = m_cmdPool;
    cbai.level = vk::VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    if (m_vk.allocateCommandBuffers(m_device, &cbai, &m_cmdBuf) != vk::VK_SUCCESS) {
        if (error) *error = "VkCommandBuffer ayrılamadı";
        return false;
    }

    vk::VkFenceCreateInfo fci{};
    if (m_vk.createFence(m_device, &fci, nullptr, &m_fence) != vk::VK_SUCCESS) {
        if (error) *error = "VkFence oluşturulamadı";
        return false;
    }

    if (!createPipelines(error)) return false;

    // UBO ve başlangıç boş tamponları
    if (!createBuffer(sizeof(gpu::GpuUniforms), vk::VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, m_uboBuf)) {
        if (error) *error = "UBO tamponu oluşturulamadı";
        return false;
    }
    const uint32_t dummy[16] = {};
    uploadBuffer(dummy, sizeof(dummy), vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_primBuf);
    uploadBuffer(dummy, sizeof(dummy), vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_bvhBuf);
    uploadBuffer(dummy, sizeof(dummy), vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_matBuf);
    uploadBuffer(dummy, sizeof(dummy), vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_lightBuf);
    uploadBuffer(dummy, sizeof(dummy), vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_texInfoBuf);
    uploadBuffer(dummy, sizeof(dummy), vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_texPixBuf);
    uploadBuffer(dummy, sizeof(dummy), vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_envPixBuf);
    uploadBuffer(dummy, sizeof(dummy), vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_linkBuf);
    uploadBuffer(dummy, sizeof(dummy), vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_motionBuf);
    return true;
}

uint32_t VulkanDevice::findMemoryType(uint32_t typeBits, vk::VkFlags props) const {
    for (uint32_t i = 0; i < m_memProps.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) != 0u && (m_memProps.memoryTypes[i].propertyFlags & props) == props) return i;
    }
    for (uint32_t i = 0; i < m_memProps.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) != 0u &&
            (m_memProps.memoryTypes[i].propertyFlags & vk::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u)
            return i;
    }
    return 0u;
}

bool VulkanDevice::createBuffer(vk::VkDeviceSize size, vk::VkFlags usage, GpuBuffer& out, bool deviceAddress) {
    destroyBuffer(out);
    const vk::VkDeviceSize allocSz = std::max<vk::VkDeviceSize>(size, 64ull);
    vk::VkBufferCreateInfo bci{};
    bci.size = allocSz;
    bci.usage = usage | (deviceAddress ? vk::VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT : 0u);
    bci.sharingMode = vk::VK_SHARING_MODE_EXCLUSIVE;
    if (m_vk.createBuffer(m_device, &bci, nullptr, &out.buf) != vk::VK_SUCCESS) return false;

    vk::VkMemoryRequirements req{};
    m_vk.getBufferMemoryRequirements(m_device, out.buf, &req);

    vk::VkMemoryAllocateFlagsInfo afi{};
    afi.flags = vk::VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    vk::VkMemoryAllocateInfo mai{};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = findMemoryType(req.memoryTypeBits,
                                         vk::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | vk::VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (deviceAddress) mai.pNext = &afi;

    if (m_vk.allocateMemory(m_device, &mai, nullptr, &out.mem) != vk::VK_SUCCESS) {
        m_vk.destroyBuffer(m_device, out.buf, nullptr);
        out.buf = vk::VK_NULL_HANDLE;
        return false;
    }
    if (m_vk.bindBufferMemory(m_device, out.buf, out.mem, 0) != vk::VK_SUCCESS) {
        destroyBuffer(out);
        return false;
    }
    if (m_vk.mapMemory(m_device, out.mem, 0, allocSz, 0, &out.mapped) != vk::VK_SUCCESS) {
        destroyBuffer(out);
        return false;
    }
    out.size = allocSz;
    if (deviceAddress && m_vk.getBufferDeviceAddress) {
        vk::VkBufferDeviceAddressInfo bda{};
        bda.buffer = out.buf;
        out.addr = m_vk.getBufferDeviceAddress(m_device, &bda);
    }
    return true;
}

void VulkanDevice::destroyBuffer(GpuBuffer& b) {
    if (!m_device) return;
    if (b.mapped && b.mem) {
        m_vk.unmapMemory(m_device, b.mem);
        b.mapped = nullptr;
    }
    if (b.buf) {
        m_vk.destroyBuffer(m_device, b.buf, nullptr);
        b.buf = vk::VK_NULL_HANDLE;
    }
    if (b.mem) {
        m_vk.freeMemory(m_device, b.mem, nullptr);
        b.mem = vk::VK_NULL_HANDLE;
    }
    b.size = 0;
    b.addr = 0;
}

bool VulkanDevice::uploadBuffer(const void* data, vk::VkDeviceSize bytes, vk::VkFlags usage, GpuBuffer& out,
                                bool deviceAddress) {
    const vk::VkDeviceSize need = std::max<vk::VkDeviceSize>(bytes, 64ull);
    const bool needAlloc = (out.buf == vk::VK_NULL_HANDLE || out.size < need || (deviceAddress && out.addr == 0));
    if (needAlloc) {
        if (!createBuffer(need, usage, out, deviceAddress)) return false;
        m_descriptorsDirty = true;
    }
    if (data && bytes > 0 && out.mapped) {
        std::memcpy(out.mapped, data, static_cast<size_t>(bytes));
    } else if (out.mapped) {
        std::memset(out.mapped, 0, static_cast<size_t>(std::min<vk::VkDeviceSize>(need, 64ull)));
    }
    return true;
}

bool VulkanDevice::submitAndWait(vk::VkCommandBuffer cmd) {
    m_vk.resetFences(m_device, 1, &m_fence);
    vk::VkSubmitInfo si{};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    if (m_vk.queueSubmit(m_queue, 1, &si, m_fence) != vk::VK_SUCCESS) return false;
    return m_vk.waitForFences(m_device, 1, &m_fence, vk::VK_TRUE, ~0ull) == vk::VK_SUCCESS;
}

// ═══════════════════════════════════════════════════════════════════════════
// Boru hatları (Compute + Hardware RT + 4 aşamalı Wavefront)
// ═══════════════════════════════════════════════════════════════════════════

bool VulkanDevice::createPipelines(std::string* error) {
    auto makeDsl = [&](bool withAs, bool withWf, vk::VkDescriptorSetLayout& out) -> bool {
        std::vector<vk::VkDescriptorSetLayoutBinding> b;
        auto addB = [&](uint32_t binding, vk::VkDescriptorType t) {
            vk::VkDescriptorSetLayoutBinding d{};
            d.binding = binding;
            d.descriptorType = t;
            d.descriptorCount = 1;
            d.stageFlags = vk::VK_SHADER_STAGE_COMPUTE_BIT;
            b.push_back(d);
        };
        addB(0, vk::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        for (uint32_t i = 1; i <= 12; ++i) addB(i, vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        if (withAs) addB(13, vk::VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR);
        if (withWf) {
            for (uint32_t i = 14; i <= 19; ++i) addB(i, vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }
        vk::VkDescriptorSetLayoutCreateInfo ci{};
        ci.bindingCount = static_cast<uint32_t>(b.size());
        ci.pBindings = b.data();
        return m_vk.createDescriptorSetLayout(m_device, &ci, nullptr, &out) == vk::VK_SUCCESS;
    };

    auto makeLayout = [&](vk::VkDescriptorSetLayout dsl, uint32_t pushBytes, vk::VkPipelineLayout& out) -> bool {
        vk::VkPushConstantRange pcr{};
        pcr.stageFlags = vk::VK_SHADER_STAGE_COMPUTE_BIT;
        pcr.offset = 0;
        pcr.size = pushBytes;
        vk::VkPipelineLayoutCreateInfo ci{};
        ci.setLayoutCount = 1;
        ci.pSetLayouts = &dsl;
        if (pushBytes > 0) {
            ci.pushConstantRangeCount = 1;
            ci.pPushConstantRanges = &pcr;
        }
        return m_vk.createPipelineLayout(m_device, &ci, nullptr, &out) == vk::VK_SUCCESS;
    };

    auto makePipe = [&](const uint32_t* spv, size_t spvBytes, vk::VkPipelineLayout pl, vk::VkPipeline& out) -> bool {
        vk::VkShaderModuleCreateInfo smci{};
        smci.codeSize = spvBytes;
        smci.pCode = spv;
        vk::VkShaderModule sm = vk::VK_NULL_HANDLE;
        if (m_vk.createShaderModule(m_device, &smci, nullptr, &sm) != vk::VK_SUCCESS) return false;
        vk::VkComputePipelineCreateInfo pci{};
        pci.stage.stage = vk::VK_SHADER_STAGE_COMPUTE_BIT;
        pci.stage.module = sm;
        pci.stage.pName = "main";
        pci.layout = pl;
        const vk::VkResult r = m_vk.createComputePipelines(m_device, vk::VK_NULL_HANDLE, 1, &pci, nullptr, &out);
        m_vk.destroyShaderModule(m_device, sm, nullptr);
        return r == vk::VK_SUCCESS;
    };

    if (!makeDsl(false, false, m_dslCompute) || !makeDsl(false, true, m_dslWf) ||
        !makeLayout(m_dslCompute, 0, m_plCompute) || !makeLayout(m_dslWf, 16, m_plWf)) {
        if (error) *error = "Compute tanımlayıcı düzeni oluşturulamadı";
        return false;
    }
    if (m_hasRayQuery) {
        if (!makeDsl(true, false, m_dslRt) || !makeDsl(true, true, m_dslWfRt) ||
            !makeLayout(m_dslRt, 0, m_plRt) || !makeLayout(m_dslWfRt, 16, m_plWfRt)) {
            m_hasRayQuery = false;
            m_info.hardwareRayTracing = false;
        }
    }

    if (!makePipe(shaders::kPathTracerComputeSpv, shaders::kPathTracerComputeSpvSize, m_plCompute, m_pipeCompute) ||
        !makePipe(shaders::kWfGenSpv, shaders::kWfGenSpvSize, m_plWf, m_pipeWfGen) ||
        !makePipe(shaders::kWfIntersectSpv, shaders::kWfIntersectSpvSize, m_plWf, m_pipeWfIntersect) ||
        !makePipe(shaders::kWfShadeSpv, shaders::kWfShadeSpvSize, m_plWf, m_pipeWfShade) ||
        !makePipe(shaders::kWfShadowSpv, shaders::kWfShadowSpvSize, m_plWf, m_pipeWfShadow)) {
        if (error) *error = "SPIR-V compute boru hattı oluşturulamadı";
        return false;
    }

    if (m_hasRayQuery) {
        if (!makePipe(shaders::kPathTracerRayQuerySpv, shaders::kPathTracerRayQuerySpvSize, m_plRt, m_pipeRt) ||
            !makePipe(shaders::kWfIntersectRtSpv, shaders::kWfIntersectRtSpvSize, m_plWfRt, m_pipeWfIntersectRt)) {
            m_hasRayQuery = false;
            m_info.hardwareRayTracing = false;
        }
    }

    std::vector<vk::VkDescriptorPoolSize> poolSizes = {
        {vk::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4},
        {vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 80},
    };
    if (m_hasRayQuery) poolSizes.push_back({vk::VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 4});

    vk::VkDescriptorPoolCreateInfo dpci{};
    dpci.maxSets = 4;
    dpci.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    dpci.pPoolSizes = poolSizes.data();
    if (m_vk.createDescriptorPool(m_device, &dpci, nullptr, &m_descPool) != vk::VK_SUCCESS) {
        if (error) *error = "VkDescriptorPool oluşturulamadı";
        return false;
    }

    auto allocDs = [&](vk::VkDescriptorSetLayout dsl, vk::VkDescriptorSet& out) -> bool {
        vk::VkDescriptorSetAllocateInfo ai{};
        ai.descriptorPool = m_descPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &dsl;
        return m_vk.allocateDescriptorSets(m_device, &ai, &out) == vk::VK_SUCCESS;
    };
    if (!allocDs(m_dslCompute, m_dsCompute) || !allocDs(m_dslWf, m_dsWf)) {
        if (error) *error = "VkDescriptorSet ayrılamadı";
        return false;
    }
    if (m_hasRayQuery) {
        if (!allocDs(m_dslRt, m_dsRt) || !allocDs(m_dslWfRt, m_dsWfRt)) {
            m_hasRayQuery = false;
            m_info.hardwareRayTracing = false;
        }
    }
    return true;
}

void VulkanDevice::destroyPipelines() {
    if (!m_device) return;
    auto dp = [&](vk::VkPipeline& p) { if (p) { m_vk.destroyPipeline(m_device, p, nullptr); p = vk::VK_NULL_HANDLE; } };
    auto dpl = [&](vk::VkPipelineLayout& l) { if (l) { m_vk.destroyPipelineLayout(m_device, l, nullptr); l = vk::VK_NULL_HANDLE; } };
    auto ddsl = [&](vk::VkDescriptorSetLayout& l) { if (l) { m_vk.destroyDescriptorSetLayout(m_device, l, nullptr); l = vk::VK_NULL_HANDLE; } };
    dp(m_pipeCompute);
    dp(m_pipeRt);
    dp(m_pipeWfGen);
    dp(m_pipeWfIntersect);
    dp(m_pipeWfIntersectRt);
    dp(m_pipeWfShade);
    dp(m_pipeWfShadow);
    dpl(m_plCompute);
    dpl(m_plRt);
    dpl(m_plWf);
    dpl(m_plWfRt);
    ddsl(m_dslCompute);
    ddsl(m_dslRt);
    ddsl(m_dslWf);
    ddsl(m_dslWfRt);
    if (m_descPool) {
        m_vk.destroyDescriptorPool(m_device, m_descPool, nullptr);
        m_descPool = vk::VK_NULL_HANDLE;
    }
}

void VulkanDevice::destroyTlas() {
    if (!m_device) return;
    if (m_tlas && m_vk.destroyAccelerationStructureKHR) {
        m_vk.destroyAccelerationStructureKHR(m_device, m_tlas, nullptr);
        m_tlas = vk::VK_NULL_HANDLE;
    }
    destroyBuffer(m_tlasBuf);
    destroyBuffer(m_tlasInstBuf);
}

void VulkanDevice::destroyAllBlas() {
    if (!m_device) return;
    auto freeBlas = [&](CachedBlas& blas) {
        if (blas.handle && m_vk.destroyAccelerationStructureKHR)
            m_vk.destroyAccelerationStructureKHR(m_device, blas.handle, nullptr);
        destroyBuffer(blas.asBuf);
        destroyBuffer(blas.vertBuf);
        destroyBuffer(blas.idxBuf);
    };
    for (auto& [k, blas] : m_blasCache) freeBlas(blas);
    m_blasCache.clear();
    for (auto& [k, blas] : m_standaloneBlasCache) freeBlas(blas);
    m_standaloneBlasCache.clear();
}

// ═══════════════════════════════════════════════════════════════════════════
// Donanım RT BLAS & TLAS kurulumu
// ═══════════════════════════════════════════════════════════════════════════

bool VulkanDevice::buildOrReuseBlasForMesh(const TriangleMesh* mesh, CachedBlas& out) {
    if (!mesh || mesh->numTriangles() == 0) return false;
    if (auto it = m_blasCache.find(mesh); it != m_blasCache.end()) {
        out = it->second;
        return true;
    }

    const auto& verts = mesh->positions();
    const auto& indices = mesh->indices();
    const uint32_t triCount = static_cast<uint32_t>(mesh->numTriangles());
    std::vector<float> pos;
    pos.reserve(verts.size() * 3);
    for (const Vec3f& v : verts) {
        pos.push_back(v.x);
        pos.push_back(v.y);
        pos.push_back(v.z);
    }

    CachedBlas blas;
    blas.triCount = triCount;
    const vk::VkFlags inUsage = vk::VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if (!uploadBuffer(pos.data(), pos.size() * sizeof(float), inUsage, blas.vertBuf, true) ||
        !uploadBuffer(indices.data(), indices.size() * sizeof(uint32_t), inUsage, blas.idxBuf, true)) {
        return false;
    }

    vk::VkAccelerationStructureGeometryKHR geom{};
    geom.geometryType = vk::VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geom.flags = 0; // cameraInvisible / cutout aday filtrelemesine izin ver
    geom.geometry.triangles.sType = vk::VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    geom.geometry.triangles.vertexFormat = vk::VK_FORMAT_R32G32B32_SFLOAT;
    geom.geometry.triangles.vertexData.deviceAddress = blas.vertBuf.addr;
    geom.geometry.triangles.vertexStride = 12;
    geom.geometry.triangles.maxVertex = static_cast<uint32_t>(verts.size() > 0 ? verts.size() - 1 : 0);
    geom.geometry.triangles.indexType = vk::VK_INDEX_TYPE_UINT32;
    geom.geometry.triangles.indexData.deviceAddress = blas.idxBuf.addr;

    vk::VkAccelerationStructureBuildGeometryInfoKHR bgi{};
    bgi.type = vk::VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    bgi.flags = vk::VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    bgi.mode = vk::VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    bgi.geometryCount = 1;
    bgi.pGeometries = &geom;

    vk::VkAccelerationStructureBuildSizesInfoKHR sizes{};
    m_vk.getAccelerationStructureBuildSizesKHR(m_device, vk::VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                               &bgi, &triCount, &sizes);
    if (!createBuffer(sizes.accelerationStructureSize, vk::VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
                      blas.asBuf, true)) {
        destroyBuffer(blas.vertBuf);
        destroyBuffer(blas.idxBuf);
        return false;
    }

    vk::VkAccelerationStructureCreateInfoKHR asci{};
    asci.buffer = blas.asBuf.buf;
    asci.offset = 0;
    asci.size = sizes.accelerationStructureSize;
    asci.type = vk::VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    if (m_vk.createAccelerationStructureKHR(m_device, &asci, nullptr, &blas.handle) != vk::VK_SUCCESS) {
        destroyBuffer(blas.asBuf);
        destroyBuffer(blas.vertBuf);
        destroyBuffer(blas.idxBuf);
        return false;
    }

    GpuBuffer scratch;
    if (!createBuffer(sizes.buildScratchSize + 256, vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, scratch, true)) {
        m_vk.destroyAccelerationStructureKHR(m_device, blas.handle, nullptr);
        destroyBuffer(blas.asBuf);
        destroyBuffer(blas.vertBuf);
        destroyBuffer(blas.idxBuf);
        return false;
    }
    bgi.dstAccelerationStructure = blas.handle;
    bgi.scratchData.deviceAddress = (scratch.addr + 255ull) & ~255ull;

    vk::VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = triCount;
    const vk::VkAccelerationStructureBuildRangeInfoKHR* pRange = &range;

    m_vk.resetCommandBuffer(m_cmdBuf, 0);
    vk::VkCommandBufferBeginInfo bi{};
    bi.flags = vk::VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    m_vk.beginCommandBuffer(m_cmdBuf, &bi);
    m_vk.cmdBuildAccelerationStructuresKHR(m_cmdBuf, 1, &bgi, &pRange);
    m_vk.endCommandBuffer(m_cmdBuf);
    const bool ok = submitAndWait(m_cmdBuf);
    destroyBuffer(scratch);
    if (!ok) {
        m_vk.destroyAccelerationStructureKHR(m_device, blas.handle, nullptr);
        destroyBuffer(blas.asBuf);
        destroyBuffer(blas.vertBuf);
        destroyBuffer(blas.idxBuf);
        return false;
    }

    vk::VkAccelerationStructureDeviceAddressInfoKHR dai{};
    dai.accelerationStructure = blas.handle;
    blas.deviceAddress = m_vk.getAccelerationStructureDeviceAddressKHR(m_device, &dai);
    ++m_stats.blasBuilds;
    m_blasCache[mesh] = blas;
    out = blas;
    return true;
}

bool VulkanDevice::buildBlasForStandalone(const std::vector<float>& verts, CachedBlas& out) {
    const uint32_t vertCount = static_cast<uint32_t>(verts.size() / 3);
    const uint32_t triCount = vertCount / 3;
    if (triCount == 0) return false;

    CachedBlas blas;
    blas.triCount = triCount;
    const vk::VkFlags inUsage = vk::VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if (!uploadBuffer(verts.data(), verts.size() * sizeof(float), inUsage, blas.vertBuf, true)) return false;

    vk::VkAccelerationStructureGeometryKHR geom{};
    geom.geometryType = vk::VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geom.flags = 0;
    geom.geometry.triangles.sType = vk::VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    geom.geometry.triangles.vertexFormat = vk::VK_FORMAT_R32G32B32_SFLOAT;
    geom.geometry.triangles.vertexData.deviceAddress = blas.vertBuf.addr;
    geom.geometry.triangles.vertexStride = 12;
    geom.geometry.triangles.maxVertex = vertCount - 1;
    geom.geometry.triangles.indexType = vk::VK_INDEX_TYPE_NONE_KHR;

    vk::VkAccelerationStructureBuildGeometryInfoKHR bgi{};
    bgi.type = vk::VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    bgi.flags = vk::VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    bgi.mode = vk::VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    bgi.geometryCount = 1;
    bgi.pGeometries = &geom;

    vk::VkAccelerationStructureBuildSizesInfoKHR sizes{};
    m_vk.getAccelerationStructureBuildSizesKHR(m_device, vk::VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                               &bgi, &triCount, &sizes);
    if (!createBuffer(sizes.accelerationStructureSize, vk::VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
                      blas.asBuf, true)) {
        destroyBuffer(blas.vertBuf);
        return false;
    }

    vk::VkAccelerationStructureCreateInfoKHR asci{};
    asci.buffer = blas.asBuf.buf;
    asci.offset = 0;
    asci.size = sizes.accelerationStructureSize;
    asci.type = vk::VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    if (m_vk.createAccelerationStructureKHR(m_device, &asci, nullptr, &blas.handle) != vk::VK_SUCCESS) {
        destroyBuffer(blas.asBuf);
        destroyBuffer(blas.vertBuf);
        return false;
    }

    GpuBuffer scratch;
    if (!createBuffer(sizes.buildScratchSize + 256, vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, scratch, true)) {
        m_vk.destroyAccelerationStructureKHR(m_device, blas.handle, nullptr);
        destroyBuffer(blas.asBuf);
        destroyBuffer(blas.vertBuf);
        return false;
    }
    bgi.dstAccelerationStructure = blas.handle;
    bgi.scratchData.deviceAddress = (scratch.addr + 255ull) & ~255ull;

    vk::VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = triCount;
    const vk::VkAccelerationStructureBuildRangeInfoKHR* pRange = &range;

    m_vk.resetCommandBuffer(m_cmdBuf, 0);
    vk::VkCommandBufferBeginInfo bi{};
    bi.flags = vk::VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    m_vk.beginCommandBuffer(m_cmdBuf, &bi);
    m_vk.cmdBuildAccelerationStructuresKHR(m_cmdBuf, 1, &bgi, &pRange);
    m_vk.endCommandBuffer(m_cmdBuf);
    const bool ok = submitAndWait(m_cmdBuf);
    destroyBuffer(scratch);
    if (!ok) {
        m_vk.destroyAccelerationStructureKHR(m_device, blas.handle, nullptr);
        destroyBuffer(blas.asBuf);
        destroyBuffer(blas.vertBuf);
        return false;
    }

    vk::VkAccelerationStructureDeviceAddressInfoKHR dai{};
    dai.accelerationStructure = blas.handle;
    blas.deviceAddress = m_vk.getAccelerationStructureDeviceAddressKHR(m_device, &dai);
    ++m_stats.blasBuilds;
    out = blas;
    return true;
}

bool VulkanDevice::rebuildTlas(const std::vector<StaticTlasEntry>& entries) {
    destroyTlas();
    if (!m_hasRayQuery || entries.empty()) return false;

    std::vector<vk::VkAccelerationStructureInstanceKHR> insts;
    insts.reserve(entries.size());
    for (const StaticTlasEntry& e : entries) {
        CachedBlas blas{};
        if (e.meshKey) {
            if (!buildOrReuseBlasForMesh(e.meshKey, blas)) continue;
        } else {
            uint64_t h = 14695981039346656037ull ^ (static_cast<uint64_t>(e.standaloneVerts.size()) << 32u);
            for (float v : e.standaloneVerts) {
                uint32_t bits = 0;
                std::memcpy(&bits, &v, sizeof(bits));
                h = (h ^ bits) * 1099511628211ull;
            }
            if (auto it = m_standaloneBlasCache.find(h); it != m_standaloneBlasCache.end()) {
                blas = it->second;
            } else {
                if (!buildBlasForStandalone(e.standaloneVerts, blas)) continue;
                m_standaloneBlasCache[h] = blas;
            }
        }
        vk::VkAccelerationStructureInstanceKHR ins{};
        std::memcpy(ins.transform.matrix, e.transform3x4, sizeof(float) * 12);
        ins.instanceCustomIndexAndMask = (e.primBase & 0x00FFFFFFu) | (static_cast<uint32_t>(e.visibilityMask) << 24u);
        ins.sbtOffsetAndFlags = vk::VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR << 24u;
        ins.accelerationStructureReference = blas.deviceAddress;
        insts.push_back(ins);
    }
    if (insts.empty()) return false;

    const vk::VkFlags inUsage = vk::VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if (!uploadBuffer(insts.data(), insts.size() * sizeof(vk::VkAccelerationStructureInstanceKHR),
                      inUsage, m_tlasInstBuf, true)) {
        return false;
    }

    const uint32_t instCount = static_cast<uint32_t>(insts.size());
    vk::VkAccelerationStructureGeometryKHR geom{};
    geom.geometryType = vk::VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geom.geometry.instances.sType = vk::VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geom.geometry.instances.arrayOfPointers = vk::VK_FALSE;
    geom.geometry.instances.data.deviceAddress = m_tlasInstBuf.addr;

    vk::VkAccelerationStructureBuildGeometryInfoKHR bgi{};
    bgi.type = vk::VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    bgi.flags = vk::VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    bgi.mode = vk::VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    bgi.geometryCount = 1;
    bgi.pGeometries = &geom;

    vk::VkAccelerationStructureBuildSizesInfoKHR sizes{};
    m_vk.getAccelerationStructureBuildSizesKHR(m_device, vk::VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                               &bgi, &instCount, &sizes);
    if (!createBuffer(sizes.accelerationStructureSize, vk::VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
                      m_tlasBuf, true)) {
        return false;
    }

    vk::VkAccelerationStructureCreateInfoKHR asci{};
    asci.buffer = m_tlasBuf.buf;
    asci.offset = 0;
    asci.size = sizes.accelerationStructureSize;
    asci.type = vk::VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    if (m_vk.createAccelerationStructureKHR(m_device, &asci, nullptr, &m_tlas) != vk::VK_SUCCESS) {
        destroyTlas();
        return false;
    }

    GpuBuffer scratch;
    if (!createBuffer(sizes.buildScratchSize + 256, vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, scratch, true)) {
        destroyTlas();
        return false;
    }
    bgi.dstAccelerationStructure = m_tlas;
    bgi.scratchData.deviceAddress = (scratch.addr + 255ull) & ~255ull;

    vk::VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = instCount;
    const vk::VkAccelerationStructureBuildRangeInfoKHR* pRange = &range;

    m_vk.resetCommandBuffer(m_cmdBuf, 0);
    vk::VkCommandBufferBeginInfo bi{};
    bi.flags = vk::VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    m_vk.beginCommandBuffer(m_cmdBuf, &bi);
    m_vk.cmdBuildAccelerationStructuresKHR(m_cmdBuf, 1, &bgi, &pRange);
    m_vk.endCommandBuffer(m_cmdBuf);
    const bool ok = submitAndWait(m_cmdBuf);
    destroyBuffer(scratch);
    if (!ok) {
        destroyTlas();
        return false;
    }
    ++m_stats.tlasBuilds;
    m_descriptorsDirty = true;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Sahne yükleme ve artımlı güncellemeler (Materials / Lights / Transforms)
// ═══════════════════════════════════════════════════════════════════════════

void VulkanDevice::setScene(std::shared_ptr<const Scene> scene) {
    updateScene(std::move(scene), SceneUpdateKind::Full);
}

void VulkanDevice::updateScene(std::shared_ptr<const Scene> scene, SceneUpdateKind kind) {
    if (!scene) {
        m_scene.reset();
        return;
    }
    const bool hadScene = (m_scene != nullptr);
    m_scene = std::move(scene);

    if (!hadScene || kind == SceneUpdateKind::Full) {
        ++m_stats.fullUploads;
        rebuildGeometryAndBvh(true);
        rebuildMaterialsAndTextures();
        rebuildLightsAndLinks();
        return;
    }

    switch (kind) {
        case SceneUpdateKind::Materials:
            ++m_stats.materialUpdates;
            rebuildMaterialsAndTextures();
            break;
        case SceneUpdateKind::Lights:
            ++m_stats.lightUpdates;
            rebuildLightsAndLinks();
            break;
        case SceneUpdateKind::Transforms:
            ++m_stats.transformUpdates;
            rebuildGeometryAndBvh(true); // önbellekli BLAS'lar korunur, yalnız TLAS+BVH2 yenilenir
            rebuildMaterialsAndTextures();
            rebuildLightsAndLinks();
            break;
        case SceneUpdateKind::Full:
            break;
    }
}

void VulkanDevice::rebuildGeometryAndBvh(bool keepBlasCache) {
    if (!keepBlasCache) destroyAllBlas();
    m_matMap.ordered.clear();
    m_matMap.indexOf.clear();
    m_uidToLinkSlot.clear();

    // 1. Önce Light Linking'de geçen UID'lere 1..62 arası yuvalar ver (63 = boş Include gözcüsü).
    uint32_t nextSlot = 1u;
    auto assignSlot = [&](uint64_t uid) -> uint32_t {
        if (uid == 0) return 0u;
        if (auto it = m_uidToLinkSlot.find(uid); it != m_uidToLinkSlot.end()) return it->second;
        if (nextSlot >= 63u) return 0u;
        const uint32_t s = nextSlot++;
        m_uidToLinkSlot[uid] = s;
        return s;
    };
    for (const Light* l : m_scene->lights()) {
        if (!l) continue;
        for (uint64_t uid : l->lightLink().nodeUids) assignSlot(uid);
        for (uint64_t uid : l->lightLink().shadowNodeUids) assignSlot(uid);
    }

    auto regMat = [&](const Material* m) -> uint32_t {
        if (auto it = m_matMap.indexOf.find(m); it != m_matMap.indexOf.end()) return it->second;
        const uint32_t idx = static_cast<uint32_t>(m_matMap.ordered.size());
        m_matMap.ordered.push_back(m);
        m_matMap.indexOf[m] = idx;
        return idx;
    };

    // Cryptomatte ad karması haritası (renderer.cpp ile birebir aynı)
    const auto& shapeNames = m_scene->shapeNames();
    const auto& shapeOwners = m_scene->shapeOwners();
    const auto& matNames = m_scene->materialNames();
    std::unordered_map<const Material*, float> matCryptoHash;
    size_t anonShape = 0, anonMat = 0;
    auto getMatCrypto = [&](const Material* m) -> float {
        if (!m) return 0.0f;
        if (auto it = matCryptoHash.find(m); it != matCryptoHash.end()) return it->second;
        std::string name;
        if (auto nit = matNames.find(m); nit != matNames.end() && !nit->second.empty()) name = nit->second;
        else name = "material_" + std::to_string(anonMat++);
        const float h = cryptoNameToFloat(name);
        matCryptoHash[m] = h;
        return h;
    };

    std::vector<gpu::GpuPrim> prims;
    std::vector<AABB> primBounds;
    std::vector<gpu::GpuInstanceMotion> motions;
    std::vector<uint32_t> secondaryIndices;
    std::vector<StaticTlasEntry> tlasEntries;

    // Bağımsız üçgenleri (ör. alan ışığı dörtgenleri) görünürlük maskesine göre grupla
    std::unordered_map<uint8_t, std::vector<const Triangle*>> standaloneTrisByMask;

    auto shapeCrypto = [&](const void* ptr, uint64_t uid) -> float {
        std::string name;
        if (auto it = shapeNames.find(ptr); it != shapeNames.end() && !it->second.empty()) name = it->second;
        else if (uid != 0) name = "object_" + std::to_string(uid);
        else name = "shape_" + std::to_string(anonShape++);
        return cryptoNameToFloat(name);
    };

    auto shapeMeta = [&](const Shape* s) {
        const void* ptr = s;
        uint64_t uid = 0;
        if (auto it = shapeOwners.find(ptr); it != shapeOwners.end()) uid = it->second;
        SurfaceInteraction dummy;
        dummy.hitObject = ptr;
        dummy.primIndex = 0;
        const bool camInv = m_scene->cameraInvisible(dummy);
        const bool catcher = m_scene->isShadowCatcher(dummy);
        const int emitBase = m_scene->emitterIndex(dummy);
        const uint32_t slot = assignSlot(uid);
        const uint8_t vis = s->visibilityMask();
        const float cObj = shapeCrypto(ptr, uid);
        const Color3f col = idColor(uid);
        return std::make_tuple(uid, camInv, catcher, emitBase, slot, vis, cObj, col);
    };

    for (const auto& sp : m_scene->shapes()) {
        if (!sp) continue;
        if (const auto* tri = dynamic_cast<const Triangle*>(sp.get())) {
            standaloneTrisByMask[tri->visibilityMask()].push_back(tri);
            continue;
        }
        const auto [uid, camInv, catcher, emitBase, slot, vis, cObj, objCol] = shapeMeta(sp.get());

        if (const auto* inst = dynamic_cast<const MeshInstance*>(sp.get())) {
            const TriangleMesh* mesh = inst->mesh().get();
            if (!mesh || mesh->numTriangles() == 0) continue;
            const Material* mat = inst->material() ? inst->material() : mesh->material();
            const uint32_t matIdx = regMat(mat);
            const float cMat = getMatCrypto(mat);
            const uint32_t matIdHash = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mat) & 0xFFFFFFFFu);

            const uint32_t primBase = static_cast<uint32_t>(prims.size());
            const auto& verts = mesh->positions();
            const auto& norms = mesh->normals();
            const auto& uvs = mesh->uvs();
            const auto& idx = mesh->indices();
            const size_t nTri = mesh->numTriangles();

            if (inst->hasMotion()) {
                // Hareketli örnek: yerel üçgen + GpuInstanceMotion (SLERP) + birleşik AABB
                const uint32_t motionIdx = static_cast<uint32_t>(motions.size());
                Vec3f p0, s0, p1, s1;
                Quatf q0, q1;
                decomposeTRS(inst->transform0(), p0, q0, s0);
                decomposeTRS(inst->transform1(), p1, q1, s1);
                if (q0.x * q1.x + q0.y * q1.y + q0.z * q1.z + q0.w * q1.w < 0.0f) {
                    q1 = Quatf(-q1.x, -q1.y, -q1.z, -q1.w);
                }
                gpu::GpuInstanceMotion gm{};
                gm.pos0 = makeV4(p0);
                gm.quat0 = gpu::Vec4{q0.x, q0.y, q0.z, q0.w};
                gm.scale0 = makeV4(s0);
                gm.pos1 = makeV4(p1);
                gm.quat1 = gpu::Vec4{q1.x, q1.y, q1.z, q1.w};
                gm.scale1 = makeV4(s1);
                motions.push_back(gm);

                const Transform xSteps[5] = {
                    inst->transformAt(0.0f), inst->transformAt(0.25f), inst->transformAt(0.5f),
                    inst->transformAt(0.75f), inst->transformAt(1.0f),
                };

                for (size_t t = 0; t < nTri; ++t) {
                    const uint32_t i0 = idx[t * 3 + 0], i1 = idx[t * 3 + 1], i2 = idx[t * 3 + 2];
                    const Vec3f v0 = verts[i0], v1 = verts[i1], v2 = verts[i2];
                    const Vec3f e1 = v1 - v0, e2 = v2 - v0;
                    const Vec3f gn = e1.cross(e2).normalized();
                    const Vec3f n0 = i0 < norms.size() ? norms[i0] : gn;
                    const Vec3f n1 = i1 < norms.size() ? norms[i1] : gn;
                    const Vec3f n2 = i2 < norms.size() ? norms[i2] : gn;
                    const Vec2f uv0 = i0 < uvs.size() ? uvs[i0] : Vec2f(0, 0);
                    const Vec2f uv1 = i1 < uvs.size() ? uvs[i1] : Vec2f(0, 0);
                    const Vec2f uv2 = i2 < uvs.size() ? uvs[i2] : Vec2f(0, 0);

                    const uint32_t flags = (camInv ? 2u : 0u) | (catcher ? 4u : 0u) | 8u |
                                           (static_cast<uint32_t>(vis) << 8u) | ((slot & 0xFFu) << 16u);
                    const uint32_t emitP1 = emitBase >= 0 ? static_cast<uint32_t>(emitBase + static_cast<int>(t) + 1) : 0u;
                    const uint32_t meta = ((motionIdx + 1u) << 16u) | (emitP1 & 0xFFFFu);

                    gpu::GpuPrim gp{};
                    gp.p0_mat = makeV4(v0, u32ToF32(matIdx));
                    gp.e1_flags = makeV4(e1, u32ToF32(flags));
                    gp.e2_meta = makeV4(e2, u32ToF32(meta));
                    gp.n0_u0 = makeV4(n0, uv0.x);
                    gp.n1_v0 = makeV4(n1, uv0.y);
                    gp.n2_u1 = makeV4(n2, uv1.x);
                    gp.ng_v1 = makeV4(gn, uv1.y);
                    gp.uv2_crypto = gpu::Vec4{uv2.x, uv2.y, cObj, cMat};
                    gp.objColor_p = makeC4(objCol, u32ToF32(matIdHash));

                    AABB tb = AABB::empty();
                    for (int k = 0; k < 5; ++k) {
                        tb.merge(xSteps[k].transformPoint(v0));
                        tb.merge(xSteps[k].transformPoint(v1));
                        tb.merge(xSteps[k].transformPoint(v2));
                    }
                    secondaryIndices.push_back(static_cast<uint32_t>(prims.size()));
                    prims.push_back(gp);
                    primBounds.push_back(tb);
                }
            } else {
                // Statik örnek: dünya uzayı üçgenleri + önbellekli BLAS + TLAS örneği
                const Transform& xf = inst->transform0();
                for (size_t t = 0; t < nTri; ++t) {
                    const uint32_t i0 = idx[t * 3 + 0], i1 = idx[t * 3 + 1], i2 = idx[t * 3 + 2];
                    const Vec3f v0 = xf.transformPoint(verts[i0]);
                    const Vec3f v1 = xf.transformPoint(verts[i1]);
                    const Vec3f v2 = xf.transformPoint(verts[i2]);
                    const Vec3f e1 = v1 - v0, e2 = v2 - v0;
                    const Vec3f gnLocal = (verts[i1] - verts[i0]).cross(verts[i2] - verts[i0]);
                    const Vec3f gn = gnLocal.lengthSquared() > 1e-18f ? xf.transformNormal(gnLocal).normalized()
                                                                      : e1.cross(e2).normalized();
                    const Vec3f n0 = i0 < norms.size() ? xf.transformNormal(norms[i0]).normalized() : gn;
                    const Vec3f n1 = i1 < norms.size() ? xf.transformNormal(norms[i1]).normalized() : gn;
                    const Vec3f n2 = i2 < norms.size() ? xf.transformNormal(norms[i2]).normalized() : gn;
                    const Vec2f uv0 = i0 < uvs.size() ? uvs[i0] : Vec2f(0, 0);
                    const Vec2f uv1 = i1 < uvs.size() ? uvs[i1] : Vec2f(0, 0);
                    const Vec2f uv2 = i2 < uvs.size() ? uvs[i2] : Vec2f(0, 0);

                    const uint32_t flags = (camInv ? 2u : 0u) | (catcher ? 4u : 0u) |
                                           (static_cast<uint32_t>(vis) << 8u) | ((slot & 0xFFu) << 16u);
                    const uint32_t emitP1 = emitBase >= 0 ? static_cast<uint32_t>(emitBase + static_cast<int>(t) + 1) : 0u;

                    gpu::GpuPrim gp{};
                    gp.p0_mat = makeV4(v0, u32ToF32(matIdx));
                    gp.e1_flags = makeV4(e1, u32ToF32(flags));
                    gp.e2_meta = makeV4(e2, u32ToF32(emitP1 & 0xFFFFu));
                    gp.n0_u0 = makeV4(n0, uv0.x);
                    gp.n1_v0 = makeV4(n1, uv0.y);
                    gp.n2_u1 = makeV4(n2, uv1.x);
                    gp.ng_v1 = makeV4(gn, uv1.y);
                    gp.uv2_crypto = gpu::Vec4{uv2.x, uv2.y, cObj, cMat};
                    gp.objColor_p = makeC4(objCol, u32ToF32(matIdHash));

                    AABB tb = AABB::empty();
                    tb.merge(v0);
                    tb.merge(v1);
                    tb.merge(v2);
                    prims.push_back(gp);
                    primBounds.push_back(tb);
                }

                StaticTlasEntry te{};
                te.meshKey = mesh;
                te.primBase = primBase;
                te.triCount = static_cast<uint32_t>(nTri);
                te.visibilityMask = vis;
                mat4To3x4(xf.matrix(), te.transform3x4);
                tlasEntries.push_back(std::move(te));
            }
        } else if (const auto* mesh = dynamic_cast<const TriangleMesh*>(sp.get())) {
            if (mesh->numTriangles() == 0) continue;
            const Material* mat = mesh->material();
            const uint32_t matIdx = regMat(mat);
            const float cMat = getMatCrypto(mat);
            const uint32_t matIdHash = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mat) & 0xFFFFFFFFu);

            const uint32_t primBase = static_cast<uint32_t>(prims.size());
            const auto& verts = mesh->positions();
            const auto& norms = mesh->normals();
            const auto& uvs = mesh->uvs();
            const auto& idx = mesh->indices();
            const size_t nTri = mesh->numTriangles();

            for (size_t t = 0; t < nTri; ++t) {
                const uint32_t i0 = idx[t * 3 + 0], i1 = idx[t * 3 + 1], i2 = idx[t * 3 + 2];
                const Vec3f v0 = verts[i0], v1 = verts[i1], v2 = verts[i2];
                const Vec3f e1 = v1 - v0, e2 = v2 - v0;
                const Vec3f gn = e1.cross(e2).normalized();
                const Vec3f n0 = i0 < norms.size() ? norms[i0] : gn;
                const Vec3f n1 = i1 < norms.size() ? norms[i1] : gn;
                const Vec3f n2 = i2 < norms.size() ? norms[i2] : gn;
                const Vec2f uv0 = i0 < uvs.size() ? uvs[i0] : Vec2f(0, 0);
                const Vec2f uv1 = i1 < uvs.size() ? uvs[i1] : Vec2f(0, 0);
                const Vec2f uv2 = i2 < uvs.size() ? uvs[i2] : Vec2f(0, 0);

                const uint32_t flags = (camInv ? 2u : 0u) | (catcher ? 4u : 0u) |
                                       (static_cast<uint32_t>(vis) << 8u) | ((slot & 0xFFu) << 16u);
                const uint32_t emitP1 = emitBase >= 0 ? static_cast<uint32_t>(emitBase + static_cast<int>(t) + 1) : 0u;

                gpu::GpuPrim gp{};
                gp.p0_mat = makeV4(v0, u32ToF32(matIdx));
                gp.e1_flags = makeV4(e1, u32ToF32(flags));
                gp.e2_meta = makeV4(e2, u32ToF32(emitP1 & 0xFFFFu));
                gp.n0_u0 = makeV4(n0, uv0.x);
                gp.n1_v0 = makeV4(n1, uv0.y);
                gp.n2_u1 = makeV4(n2, uv1.x);
                gp.ng_v1 = makeV4(gn, uv1.y);
                gp.uv2_crypto = gpu::Vec4{uv2.x, uv2.y, cObj, cMat};
                gp.objColor_p = makeC4(objCol, u32ToF32(matIdHash));

                AABB tb = AABB::empty();
                tb.merge(v0);
                tb.merge(v1);
                tb.merge(v2);
                prims.push_back(gp);
                primBounds.push_back(tb);
            }

            StaticTlasEntry te{};
            te.meshKey = mesh;
            te.primBase = primBase;
            te.triCount = static_cast<uint32_t>(nTri);
            te.visibilityMask = vis;
            identity3x4(te.transform3x4);
            tlasEntries.push_back(std::move(te));
        } else if (const auto* sph = dynamic_cast<const Sphere*>(sp.get())) {
            const Material* mat = sph->material();
            const uint32_t matIdx = regMat(mat);
            const float cMat = getMatCrypto(mat);
            const uint32_t matIdHash = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mat) & 0xFFFFFFFFu);

            const uint32_t flags = 1u | (camInv ? 2u : 0u) | (catcher ? 4u : 0u) |
                                   (static_cast<uint32_t>(vis) << 8u) | ((slot & 0xFFu) << 16u);
            const uint32_t emitP1 = emitBase >= 0 ? static_cast<uint32_t>(emitBase + 1) : 0u;

            gpu::GpuPrim gp{};
            gp.p0_mat = makeV4(sph->center(), u32ToF32(matIdx));
            gp.e1_flags = gpu::Vec4{sph->radius(), 0.0f, 0.0f, u32ToF32(flags)};
            gp.e2_meta = gpu::Vec4{0.0f, 0.0f, 0.0f, u32ToF32(emitP1 & 0xFFFFu)};
            gp.uv2_crypto = gpu::Vec4{0.0f, 0.0f, cObj, cMat};
            gp.objColor_p = makeC4(objCol, u32ToF32(matIdHash));

            secondaryIndices.push_back(static_cast<uint32_t>(prims.size()));
            prims.push_back(gp);
            primBounds.push_back(sph->bounds());
        }
    }

    // Bağımsız üçgenler (ör. Alan ışığı dörtgenleri): maske başına bitişik yerleştir
    for (const auto& [mask, tris] : standaloneTrisByMask) {
        if (tris.empty()) continue;
        StaticTlasEntry te{};
        te.meshKey = nullptr;
        te.primBase = static_cast<uint32_t>(prims.size());
        te.triCount = static_cast<uint32_t>(tris.size());
        te.visibilityMask = mask;
        identity3x4(te.transform3x4);
        te.standaloneVerts.reserve(tris.size() * 9);

        for (const Triangle* tri : tris) {
            const auto [uid, camInv, catcher, emitBase, slot, vis, cObj, objCol] = shapeMeta(tri);
            const Material* mat = tri->material();
            const uint32_t matIdx = regMat(mat);
            const float cMat = getMatCrypto(mat);
            const uint32_t matIdHash = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mat) & 0xFFFFFFFFu);

            const Vec3f v0 = tri->v0(), v1 = tri->v1(), v2 = tri->v2();
            const Vec3f e1 = v1 - v0, e2 = v2 - v0;
            const Vec3f gn = e1.cross(e2).normalized();
            const uint32_t flags = (camInv ? 2u : 0u) | (catcher ? 4u : 0u) |
                                   (static_cast<uint32_t>(vis) << 8u) | ((slot & 0xFFu) << 16u);
            const uint32_t emitP1 = emitBase >= 0 ? static_cast<uint32_t>(emitBase + 1) : 0u;

            gpu::GpuPrim gp{};
            gp.p0_mat = makeV4(v0, u32ToF32(matIdx));
            gp.e1_flags = makeV4(e1, u32ToF32(flags));
            gp.e2_meta = makeV4(e2, u32ToF32(emitP1 & 0xFFFFu));
            gp.n0_u0 = makeV4(tri->n0(), tri->uv0().x);
            gp.n1_v0 = makeV4(tri->n1(), tri->uv0().y);
            gp.n2_u1 = makeV4(tri->n2(), tri->uv1().x);
            gp.ng_v1 = makeV4(gn, tri->uv1().y);
            gp.uv2_crypto = gpu::Vec4{tri->uv2().x, tri->uv2().y, cObj, cMat};
            gp.objColor_p = makeC4(objCol, u32ToF32(matIdHash));

            prims.push_back(gp);
            primBounds.push_back(tri->bounds());

            for (const Vec3f& v : {v0, v1, v2}) {
                te.standaloneVerts.push_back(v.x);
                te.standaloneVerts.push_back(v.y);
                te.standaloneVerts.push_back(v.z);
            }
        }
        tlasEntries.push_back(std::move(te));
    }

    // SAH BVH2 kur (birincil = tüm ilkeller; ikincil = RT modunda küreler + hareketli mesh'ler)
    std::vector<gpu::GpuBvhNode> bvhNodes;
    m_secBvhRoot = 0;
    m_hasSecBvh = false;
    if (!prims.empty()) {
        bvhNodes.reserve(prims.size() * 2 + secondaryIndices.size() * 2);
        std::vector<uint32_t> allIdx(prims.size());
        std::iota(allIdx.begin(), allIdx.end(), 0u);
        buildSahBvh2(primBounds, allIdx, 0, allIdx.size(), bvhNodes);
        if (!secondaryIndices.empty()) {
            m_secBvhRoot = buildSahBvh2(primBounds, secondaryIndices, 0, secondaryIndices.size(), bvhNodes);
            m_hasSecBvh = true;
        }
    }

    m_primCount = static_cast<uint32_t>(prims.size());
    m_bvhNodeCount = static_cast<uint32_t>(bvhNodes.size());
    uploadBuffer(prims.data(), prims.size() * sizeof(gpu::GpuPrim), vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_primBuf);
    uploadBuffer(bvhNodes.data(), bvhNodes.size() * sizeof(gpu::GpuBvhNode), vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_bvhBuf);
    uploadBuffer(motions.data(), motions.size() * sizeof(gpu::GpuInstanceMotion), vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_motionBuf);

    // Donanım RT TLAS kur (önbellekli BLAS'ları yeniden kullanır!)
    if (m_hasRayQuery) {
        rebuildTlas(tlasEntries);
    }
    m_descriptorsDirty = true;
}

void VulkanDevice::rebuildMaterialsAndTextures() {
    std::vector<gpu::GpuMaterial> mats;
    mats.reserve(std::max<size_t>(1, m_matMap.ordered.size()));

    std::vector<gpu::GpuTexInfo> texInfos;
    std::vector<uint32_t> texPixels;
    std::unordered_map<const Image*, int32_t> texMap;

    auto packTex = [&](const std::shared_ptr<const Image>& img, bool isSrgb) -> int32_t {
        if (!img || img->width() <= 0 || img->height() <= 0) return -1;
        if (auto it = texMap.find(img.get()); it != texMap.end()) return it->second;
        const int32_t id = static_cast<int32_t>(texInfos.size());
        const uint32_t w = static_cast<uint32_t>(img->width());
        const uint32_t h = static_cast<uint32_t>(img->height());
        const uint32_t offset = static_cast<uint32_t>(texPixels.size());
        texPixels.resize(static_cast<size_t>(offset) + static_cast<size_t>(w) * h);
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                const Color3f c = img->getPixel(static_cast<int>(x), static_cast<int>(y));
                auto enc = [isSrgb](float v) -> uint32_t {
                    v = std::clamp(v, 0.0f, 1.0f);
                    if (isSrgb) {
                        v = v <= 0.0031308f ? (12.92f * v) : (1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f);
                    }
                    return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
                };
                const uint32_t r = enc(c.r), g = enc(c.g), b = enc(c.b);
                texPixels[static_cast<size_t>(offset) + static_cast<size_t>(y) * w + x] =
                    r | (g << 8u) | (b << 16u) | (255u << 24u);
            }
        }
        gpu::GpuTexInfo ti{};
        ti.info = gpu::UVec4{w, h, offset, isSrgb ? 1u : 0u};
        texInfos.push_back(ti);
        texMap[img.get()] = id;
        return id;
    };

    SurfaceInteraction dummySi{};
    for (const Material* mat : m_matMap.ordered) {
        gpu::GpuMaterial gm{};
        gm.base_metallic = gpu::Vec4{0.7f, 0.7f, 0.7f, 0.0f};
        gm.rough_spec_ior_t = gpu::Vec4{0.5f, 0.5f, 1.5f, 0.0f};
        gm.coat_cr_ci_sheen = gpu::Vec4{0.0f, 0.03f, 1.5f, 0.0f};
        gm.coatTint_opacity = gpu::Vec4{1.0f, 1.0f, 1.0f, 1.0f};
        gm.sheenCol_type = gpu::Vec4{1.0f, 1.0f, 1.0f, u32ToF32(1u)};
        gm.emit_flags = gpu::Vec4{0.0f, 0.0f, 0.0f, 0.0f};
        gm.tex0 = gpu::Vec4{i32ToF32(-1), i32ToF32(-1), i32ToF32(-1), i32ToF32(-1)};
        gm.tex1 = gpu::Vec4{i32ToF32(-1), 1.0f, 0.0f, 0.0f};

        if (const auto* d = dynamic_cast<const DisneyMaterial*>(mat)) {
            const Color3f emit = d->emitted(dummySi);
            uint32_t mf = 0u;
            if (d->textureMapping() == DisneyMaterial::TextureMapping::Box) mf |= 2u;
            if (d->transmitsShadows()) mf |= 4u;
            gm.base_metallic = makeC4(d->baseColor(), d->metallic());
            gm.rough_spec_ior_t = gpu::Vec4{d->roughness(), d->specular(), d->ior(), d->specularTransmission()};
            gm.coat_cr_ci_sheen = gpu::Vec4{d->clearCoat(), d->clearCoatRoughness(), d->coatIor(), d->sheen()};
            gm.coatTint_opacity = makeC4(d->coatTint(), d->baseOpacity());
            const Color3f sc = d->hasCustomSheenColor() ? d->sheenColor() : Color3f(1.0f);
            gm.sheenCol_type = makeC4(sc, u32ToF32(0u));
            gm.emit_flags = makeC4(emit, u32ToF32(mf));
            gm.tex0 = gpu::Vec4{
                i32ToF32(packTex(d->albedoImage(), true)),
                i32ToF32(packTex(d->roughnessImage(), false)),
                i32ToF32(packTex(d->metalnessImage(), false)),
                i32ToF32(packTex(d->normalImage(), false)),
            };
            gm.tex1 = gpu::Vec4{i32ToF32(packTex(d->opacityImage(), false)), d->textureScale(), 0.0f, d->anisotropy()};
        } else if (const auto* l = dynamic_cast<const Lambertian*>(mat)) {
            const Color3f emit = l->emitted(dummySi);
            gm.base_metallic = makeC4(l->albedo(), 0.0f);
            gm.sheenCol_type = gpu::Vec4{1.0f, 1.0f, 1.0f, u32ToF32(1u)};
            gm.emit_flags = makeC4(emit, 0.0f);
        } else if (const auto* g = dynamic_cast<const Dielectric*>(mat)) {
            uint32_t mf = (g->thinWalled() ? 1u : 0u) | (g->transmitsShadows() ? 4u : 0u);
            gm.base_metallic = makeC4(g->tint(), 0.0f);
            gm.rough_spec_ior_t = gpu::Vec4{g->roughness(), 0.5f, g->ior(), 1.0f};
            gm.sheenCol_type = gpu::Vec4{1.0f, 1.0f, 1.0f, u32ToF32(2u)};
            gm.emit_flags = gpu::Vec4{0.0f, 0.0f, 0.0f, u32ToF32(mf)};
        } else if (const auto* m = dynamic_cast<const Mirror*>(mat)) {
            gm.base_metallic = makeC4(m->reflectance(), 1.0f);
            gm.rough_spec_ior_t = gpu::Vec4{0.0f, 1.0f, 1.5f, 0.0f};
            gm.sheenCol_type = gpu::Vec4{1.0f, 1.0f, 1.0f, u32ToF32(3u)};
        }
        mats.push_back(gm);
    }

    uploadBuffer(mats.data(), mats.size() * sizeof(gpu::GpuMaterial), vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_matBuf);
    uploadBuffer(texInfos.data(), texInfos.size() * sizeof(gpu::GpuTexInfo), vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_texInfoBuf);
    uploadBuffer(texPixels.data(), texPixels.size() * sizeof(uint32_t), vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_texPixBuf);
}

void VulkanDevice::rebuildLightsAndLinks() {
    const auto& srcLights = m_scene->lights();
    std::vector<gpu::GpuLight> gpuLights;
    std::vector<gpu::GpuLinkEntry> linkTable;
    gpuLights.reserve(srcLights.size());
    linkTable.reserve(srcLights.size());

    for (size_t i = 0; i < srcLights.size(); ++i) {
        const Light* l = srcLights[i];
        gpu::GpuLight gl{};
        const int gIdx = std::max(0, m_scene->lightGroupIndex(static_cast<int>(i)));
        uint32_t type = 0u;

        if (const auto* al = dynamic_cast<const AreaLight*>(l)) {
            type = 0u;
            gl.pos_type = makeV4(al->position(), 0.0f);
            gl.u_area = makeV4(al->u(), al->area());
            gl.v_cosIn = makeV4(al->v(), 0.0f);
            gl.rad_cosOut = makeC4(al->radiance(), 0.0f);
            gl.norm_power = makeV4(al->normal(), al->power().luminance());
        } else if (const auto* tl = dynamic_cast<const TriangleLight*>(l)) {
            type = 4u;
            gl.pos_type = makeV4(tl->p0(), 0.0f);
            gl.u_area = makeV4(tl->p1() - tl->p0(), tl->area());
            gl.v_cosIn = makeV4(tl->p2() - tl->p0(), 0.0f);
            gl.rad_cosOut = makeC4(tl->radiance(), 0.0f);
            gl.norm_power = makeV4(tl->normal(), tl->power().luminance());
        } else if (const auto* pl = dynamic_cast<const PointLight*>(l)) {
            type = 1u;
            gl.pos_type = makeV4(pl->position(), 0.0f);
            gl.rad_cosOut = makeC4(pl->intensity(), 0.0f);
            gl.norm_power = gpu::Vec4{0, 1, 0, pl->power().luminance()};
        } else if (const auto* dl = dynamic_cast<const DirectionalLight*>(l)) {
            type = 2u;
            gl.pos_type = makeV4(dl->direction(), 0.0f);
            gl.rad_cosOut = makeC4(dl->irradiance(), 0.0f);
            gl.norm_power = gpu::Vec4{0, 1, 0, dl->power().luminance()};
        } else if (const auto* sl = dynamic_cast<const SpotLight*>(l)) {
            type = 3u;
            gl.pos_type = makeV4(sl->position(), 0.0f);
            gl.u_area = makeV4(sl->direction(), 0.0f);
            gl.v_cosIn = gpu::Vec4{0, 0, 0, sl->cosInner()};
            gl.rad_cosOut = makeC4(sl->intensity(), sl->cosOuter());
            gl.norm_power = gpu::Vec4{0, 1, 0, sl->power().luminance()};
        }
        const uint32_t meta = (type & 0xFFu) | ((static_cast<uint32_t>(gIdx) & 0xFFu) << 8u) |
                              ((static_cast<uint32_t>(i) & 0xFFFFu) << 16u);
        gl.pos_type.w = u32ToF32(meta);
        gpuLights.push_back(gl);

        // O(1) Light Linking & Shadow Linking 64-bit maskesi
        gpu::GpuLinkEntry le{};
        if (l) {
            const LightLinkSet& ll = l->lightLink();
            if (ll.mode == LightLinkSet::Mode::Include) {
                bool any = false;
                for (uint64_t uid : ll.nodeUids) {
                    if (auto it = m_uidToLinkSlot.find(uid); it != m_uidToLinkSlot.end()) {
                        setBit64(le.lightMask.x, le.lightMask.y, it->second);
                        any = true;
                    }
                }
                if (!any) setBit64(le.lightMask.x, le.lightMask.y, 63u); // boş Include: hiçbir nesne eşleşmez
            } else if (ll.mode == LightLinkSet::Mode::Exclude) {
                for (uint64_t uid : ll.nodeUids) {
                    if (auto it = m_uidToLinkSlot.find(uid); it != m_uidToLinkSlot.end())
                        setBit64(le.lightMask.z, le.lightMask.w, it->second);
                }
            }
            if (ll.shadowMode == LightLinkSet::Mode::Include) {
                bool any = false;
                for (uint64_t uid : ll.shadowNodeUids) {
                    if (auto it = m_uidToLinkSlot.find(uid); it != m_uidToLinkSlot.end()) {
                        setBit64(le.shadowMask.x, le.shadowMask.y, it->second);
                        any = true;
                    }
                }
                if (!any) setBit64(le.shadowMask.x, le.shadowMask.y, 63u);
            } else if (ll.shadowMode == LightLinkSet::Mode::Exclude) {
                for (uint64_t uid : ll.shadowNodeUids) {
                    if (auto it = m_uidToLinkSlot.find(uid); it != m_uidToLinkSlot.end())
                        setBit64(le.shadowMask.z, le.shadowMask.w, it->second);
                }
            }
        }
        linkTable.push_back(le);
    }

    m_lightCount = static_cast<uint32_t>(gpuLights.size());
    uploadBuffer(gpuLights.data(), gpuLights.size() * sizeof(gpu::GpuLight), vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_lightBuf);
    uploadBuffer(linkTable.data(), linkTable.size() * sizeof(gpu::GpuLinkEntry), vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_linkBuf);

    // Ortam haritası
    const EnvironmentLight* env = m_scene->environment();
    m_hasEnv = (env != nullptr);
    m_envW = 0;
    m_envH = 0;
    std::vector<gpu::Vec4> envPx;
    if (env && env->image() && env->image()->width() > 0 && env->image()->height() > 0) {
        const Image& img = *env->image();
        m_envW = static_cast<uint32_t>(img.width());
        m_envH = static_cast<uint32_t>(img.height());
        envPx.resize(static_cast<size_t>(m_envW) * m_envH);
        for (uint32_t y = 0; y < m_envH; ++y) {
            for (uint32_t x = 0; x < m_envW; ++x) {
                const Color3f c = img.getPixel(static_cast<int>(x), static_cast<int>(y));
                envPx[static_cast<size_t>(y) * m_envW + x] = gpu::Vec4{c.r, c.g, c.b, 1.0f};
            }
        }
    }
    uploadBuffer(envPx.data(), envPx.size() * sizeof(gpu::Vec4), vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_envPixBuf);
}

// ═══════════════════════════════════════════════════════════════════════════
// Çıktı tamponları, tanımlayıcı güncellemesi ve render
// ═══════════════════════════════════════════════════════════════════════════

bool VulkanDevice::ensureOutputBuffers(int w, int h, bool wantAovs, bool wantPasses, bool wantWavefront) {
    const size_t pix = static_cast<size_t>(w) * static_cast<size_t>(h);
    const bool sizeChanged = (w != m_outW || h != m_outH);
    const bool needAovAlloc = wantAovs && (!m_outHasAovs || sizeChanged);
    const bool needPassAlloc = wantPasses && (!m_outHasPasses || sizeChanged);
    const bool needWfAlloc = wantWavefront && (!m_outHasWf || sizeChanged);

    if (sizeChanged || m_outBeautyBuf.buf == vk::VK_NULL_HANDLE) {
        if (!createBuffer(pix * sizeof(gpu::Vec4), vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_outBeautyBuf)) return false;
        m_descriptorsDirty = true;
    }
    if (needAovAlloc || m_outAovBuf.buf == vk::VK_NULL_HANDLE) {
        const size_t sz = wantAovs ? (pix * 3 * sizeof(gpu::Vec4)) : 64;
        if (!createBuffer(sz, vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_outAovBuf)) return false;
        m_outHasAovs = wantAovs;
        m_descriptorsDirty = true;
    }
    if (needPassAlloc || m_outPassBuf.buf == vk::VK_NULL_HANDLE) {
        const size_t sz = wantPasses ? (pix * gpu::kGpuPassVec4Stride * sizeof(gpu::Vec4)) : 64;
        if (!createBuffer(sz, vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_outPassBuf)) return false;
        m_outHasPasses = wantPasses;
        m_descriptorsDirty = true;
    }
    if (needWfAlloc || (wantWavefront && m_wfPathBuf.buf == vk::VK_NULL_HANDLE)) {
        const vk::VkFlags wfUsage = vk::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | vk::VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (!createBuffer(pix * sizeof(gpu::GpuPathState), wfUsage, m_wfPathBuf) ||
            !createBuffer(pix * sizeof(gpu::GpuRayWork), wfUsage, m_wfRayABuf) ||
            !createBuffer(pix * sizeof(gpu::GpuRayWork), wfUsage, m_wfRayBBuf) ||
            !createBuffer(pix * sizeof(gpu::GpuHitWork), wfUsage, m_wfHitBuf) ||
            !createBuffer(pix * sizeof(gpu::GpuShadowWork), wfUsage, m_wfShadowBuf) ||
            !createBuffer(64, wfUsage, m_wfCntBuf)) {
            return false;
        }
        m_outHasWf = true;
        m_descriptorsDirty = true;
    } else if (m_wfPathBuf.buf == vk::VK_NULL_HANDLE) {
        // Wavefront tamponları henüz ayrılmadıysa en azından küçük yer tutucu oluşturma gerekmez;
        // m_dsWf yalnız wantWavefront iken güncellenir.
    }
    m_outW = w;
    m_outH = h;
    return true;
}

void VulkanDevice::updateDescriptorSets(bool wantWavefront) {
    auto writeSet = [&](vk::VkDescriptorSet ds, bool withAs, bool withWf) {
        if (!ds) return;
        std::array<vk::VkDescriptorBufferInfo, 20> bi{};
        std::vector<vk::VkWriteDescriptorSet> writes;
        writes.reserve(20);
        auto addBuf = [&](uint32_t b, vk::VkDescriptorType t, const GpuBuffer& gb) {
            bi[b] = vk::VkDescriptorBufferInfo{gb.buf, 0, vk::VK_WHOLE_SIZE};
            vk::VkWriteDescriptorSet w{};
            w.dstSet = ds;
            w.dstBinding = b;
            w.descriptorCount = 1;
            w.descriptorType = t;
            w.pBufferInfo = &bi[b];
            writes.push_back(w);
        };
        addBuf(0, vk::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, m_uboBuf);
        addBuf(1, vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_primBuf);
        addBuf(2, vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_bvhBuf);
        addBuf(3, vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_matBuf);
        addBuf(4, vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_lightBuf);
        addBuf(5, vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_texInfoBuf);
        addBuf(6, vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_texPixBuf);
        addBuf(7, vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_envPixBuf);
        addBuf(8, vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_outBeautyBuf);
        addBuf(9, vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_outAovBuf);
        addBuf(10, vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_linkBuf);
        addBuf(11, vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_motionBuf);
        addBuf(12, vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_outPassBuf);

        vk::VkWriteDescriptorSetAccelerationStructureKHR asWrite{};
        if (withAs && m_tlas != vk::VK_NULL_HANDLE) {
            asWrite.accelerationStructureCount = 1;
            asWrite.pAccelerationStructures = &m_tlas;
            vk::VkWriteDescriptorSet w{};
            w.pNext = &asWrite;
            w.dstSet = ds;
            w.dstBinding = 13;
            w.descriptorCount = 1;
            w.descriptorType = vk::VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            writes.push_back(w);
        }
        if (withWf && m_wfPathBuf.buf != vk::VK_NULL_HANDLE) {
            addBuf(14, vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_wfPathBuf);
            addBuf(15, vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_wfRayABuf);
            addBuf(16, vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_wfRayBBuf);
            addBuf(17, vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_wfHitBuf);
            addBuf(18, vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_wfShadowBuf);
            addBuf(19, vk::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, m_wfCntBuf);
        }
        m_vk.updateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    };

    writeSet(m_dsCompute, false, false);
    if (m_hasRayQuery && m_tlas != vk::VK_NULL_HANDLE) writeSet(m_dsRt, true, false);
    if (wantWavefront) {
        writeSet(m_dsWf, false, true);
        if (m_hasRayQuery && m_tlas != vk::VK_NULL_HANDLE) writeSet(m_dsWfRt, true, true);
    }
    m_descriptorsDirty = false;
}

SampleBatchResult VulkanDevice::render(const Camera& camera, const RenderSettings& settings, RenderTargets& targets,
                                       int firstSample, int count, const std::atomic<bool>* cancel) {
    SampleBatchResult res;
    if (!m_scene || !targets.beauty || count <= 0) return SampleBatchResult{false, 0};
    if (cancel && cancel->load(std::memory_order_relaxed)) return SampleBatchResult{false, 0};

    const int w = targets.beauty->width();
    const int h = targets.beauty->height();
    if (w <= 0 || h <= 0) return res;

    const bool wantAovs = (targets.aovs != nullptr);
    const bool wantPasses = (targets.passes != nullptr);
    if (wantAovs && (targets.aovs->albedo.width() != w || targets.aovs->albedo.height() != h))
        targets.aovs->resize(w, h);
    if (wantPasses) {
        if (targets.passes->depth.width() != w || targets.passes->depth.height() != h) targets.passes->resize(w, h);
        const auto& lgNames = m_scene->lightGroupNames();
        bool lgOk = (targets.passes->lightGroups.size() == lgNames.size());
        for (size_t i = 0; lgOk && i < lgNames.size(); ++i)
            lgOk = targets.passes->lightGroups[i].first == lgNames[i] &&
                   targets.passes->lightGroups[i].second.width() == w &&
                   targets.passes->lightGroups[i].second.height() == h;
        if (!lgOk) targets.passes->initLightGroups(w, h, lgNames);

        // Cryptomatte manifest ön-kayıt (CPU renderer.cpp ile birebir aynı)
        const auto& shapeNames = m_scene->shapeNames();
        const auto& shapeOwners = m_scene->shapeOwners();
        const auto& matNames = m_scene->materialNames();
        size_t anonShape = 0, anonMat = 0;
        std::unordered_set<const Material*> seenMats;
        auto regMatName = [&](const Material* m) {
            if (!m || !seenMats.insert(m).second) return;
            if (auto it = matNames.find(m); it != matNames.end() && !it->second.empty())
                targets.passes->cryptoMaterial.registerName(it->second);
            else
                targets.passes->cryptoMaterial.registerName("material_" + std::to_string(anonMat++));
        };
        for (const auto& [m, name] : matNames)
            if (m && !name.empty()) { seenMats.insert(m); targets.passes->cryptoMaterial.registerName(name); }
        for (const auto& sp : m_scene->shapes()) {
            if (!sp) continue;
            const void* ptr = sp.get();
            if (auto it = shapeNames.find(ptr); it != shapeNames.end() && !it->second.empty())
                targets.passes->cryptoObject.registerName(it->second);
            else if (auto oit = shapeOwners.find(ptr); oit != shapeOwners.end() && oit->second != 0)
                targets.passes->cryptoObject.registerName("object_" + std::to_string(oit->second));
            else
                targets.passes->cryptoObject.registerName("shape_" + std::to_string(anonShape++));
            if (const auto* inst = dynamic_cast<const MeshInstance*>(sp.get())) {
                regMatName(inst->material());
                if (inst->mesh()) regMatName(inst->mesh()->material());
            } else if (const auto* mesh = dynamic_cast<const TriangleMesh*>(sp.get())) regMatName(mesh->material());
            else if (const auto* sph = dynamic_cast<const Sphere*>(sp.get())) regMatName(sph->material());
            else if (const auto* tri = dynamic_cast<const Triangle*>(sp.get())) regMatName(tri->material());
        }
    }

    const bool useWavefront = (settings.gpuMode == GpuMode::Wavefront);
    const bool useRt = m_hasRayQuery && (m_tlas != vk::VK_NULL_HANDLE) &&
                       (settings.gpuMode == GpuMode::Auto || settings.gpuMode == GpuMode::RayQuery);
    if (!ensureOutputBuffers(w, h, wantAovs, wantPasses, useWavefront)) return SampleBatchResult{false, 0};
    if (m_descriptorsDirty || useWavefront) updateDescriptorSets(useWavefront);

    // UBO doldur
    const CameraGpuDesc cam0 = camera.describeGpu();
    const CameraGpuDesc cam1 = camera.describeGpuEnd();
    const EnvironmentLight* env = m_scene->environment();
    const Background& bg = m_scene->background();

    uint32_t bitFlags = 0u;
    if (settings.motionBlur) bitFlags |= 1u;
    if (m_hasEnv) bitFlags |= 2u;
    if (env && env->hasSun()) bitFlags |= 4u;
    if (settings.motionBlur && camera.hasShutterEnd()) bitFlags |= 8u;
    if (m_hasSecBvh) bitFlags |= 16u;
    if (m_scene->hasShadowLinks()) bitFlags |= 32u;
    if (m_scene->hasLightLinks()) bitFlags |= 64u;

    gpu::GpuUniforms ubo{};
    ubo.dims = gpu::UVec4{static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                          static_cast<uint32_t>(firstSample), static_cast<uint32_t>(count)};
    ubo.counts = gpu::UVec4{m_primCount, m_bvhNodeCount, m_lightCount, static_cast<uint32_t>(std::max(1, settings.maxBounces))};
    ubo.envInfo = gpu::UVec4{m_envW, m_envH, static_cast<uint32_t>(bg.mode),
                             static_cast<uint32_t>(std::max(0, m_scene->envLightGroupIndex()))};
    ubo.flags = gpu::UVec4{wantAovs ? 1u : 0u, wantPasses ? 1u : 0u,
                           static_cast<uint32_t>(m_scene->lightGroupNames().size()), bitFlags};
    ubo.camPos_lens = makeV4(cam0.position, cam0.lensRadius);
    ubo.camLL_ortho = makeV4(cam0.lowerLeft, cam0.orthographic ? 1.0f : 0.0f);
    ubo.camHoriz_ao = makeV4(cam0.horizontal, settings.aoStrength);
    ubo.camVert_clamp = makeV4(cam0.vertical, settings.clampIndirect);
    ubo.camU_rot = makeV4(cam0.u, env ? env->rotation() : 0.0f);
    ubo.camV_int = makeV4(cam0.v, env ? env->intensity() : 0.0f);
    ubo.camFwd_shadowQ = makeV4(cam0.forward, static_cast<float>(std::max(1, settings.shadowQuality)));
    ubo.bgColor_filter = makeC4(bg.color, settings.filter.width);
    if (env && env->hasSun()) {
        ubo.sunDir_cosMax = makeV4(env->sunDirection(), env->sunCosMax());
        ubo.sunRad_ftype = makeC4(env->sunRadiance(), static_cast<float>(settings.filter.type));
    } else {
        ubo.sunRad_ftype.w = static_cast<float>(settings.filter.type);
    }
    ubo.camEndPos_lens = makeV4(cam1.position, cam1.lensRadius);
    ubo.camEndLL_sec = makeV4(cam1.lowerLeft, u32ToF32(m_secBvhRoot));
    ubo.camEndHoriz_p = makeV4(cam1.horizontal);
    ubo.camEndVert_p = makeV4(cam1.vertical);
    ubo.camEndFwd_p = makeV4(cam1.forward);
    std::memcpy(m_uboBuf.mapped, &ubo, sizeof(ubo));

    // Komut tamponu kaydı
    m_vk.resetCommandBuffer(m_cmdBuf, 0);
    vk::VkCommandBufferBeginInfo bi{};
    bi.flags = vk::VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    m_vk.beginCommandBuffer(m_cmdBuf, &bi);

    auto memBarrier = [&](vk::VkFlags srcStage, vk::VkFlags srcAccess, vk::VkFlags dstStage, vk::VkFlags dstAccess) {
        vk::VkMemoryBarrier mb{};
        mb.srcAccessMask = srcAccess;
        mb.dstAccessMask = dstAccess;
        m_vk.cmdPipelineBarrier(m_cmdBuf, srcStage, dstStage, 0, 1, &mb, 0, nullptr, 0, nullptr);
    };

    if (useWavefront) {
        const uint32_t totalPix = static_cast<uint32_t>(w) * static_cast<uint32_t>(h);
        const uint32_t groups = (totalPix + 63u) / 64u;
        const bool wfUseRt = m_hasRayQuery && (m_tlas != vk::VK_NULL_HANDLE);
        const uint32_t maxB = static_cast<uint32_t>(std::max(1, settings.maxBounces));
        for (int s = 0; s < count; ++s) {
            m_vk.cmdFillBuffer(m_cmdBuf, m_wfCntBuf.buf, 0, 16, 0);
            memBarrier(vk::VK_PIPELINE_STAGE_TRANSFER_BIT, vk::VK_ACCESS_TRANSFER_WRITE_BIT,
                       vk::VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       vk::VK_ACCESS_SHADER_READ_BIT | vk::VK_ACCESS_SHADER_WRITE_BIT);

            uint32_t pc[4] = {0u, static_cast<uint32_t>(firstSample + s), 0u, s == 0 ? 1u : 0u};
            m_vk.cmdBindPipeline(m_cmdBuf, vk::VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeWfGen);
            m_vk.cmdBindDescriptorSets(m_cmdBuf, vk::VK_PIPELINE_BIND_POINT_COMPUTE, m_plWf, 0, 1, &m_dsWf, 0, nullptr);
            m_vk.cmdPushConstants(m_cmdBuf, m_plWf, vk::VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, pc);
            m_vk.cmdDispatch(m_cmdBuf, groups, 1, 1);
            memBarrier(vk::VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, vk::VK_ACCESS_SHADER_WRITE_BIT,
                       vk::VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       vk::VK_ACCESS_SHADER_READ_BIT | vk::VK_ACCESS_SHADER_WRITE_BIT);

            for (uint32_t bounce = 0; bounce <= maxB; ++bounce) {
                const uint32_t ping = bounce & 1u;
                pc[0] = bounce;
                pc[2] = ping;
                pc[3] = 0u;

                // Sonraki kuyruk ve gölge sayacını sıfırla (yarışmasız)
                m_vk.cmdFillBuffer(m_cmdBuf, m_wfCntBuf.buf, static_cast<vk::VkDeviceSize>((1u - ping) * 4u), 4, 0);
                m_vk.cmdFillBuffer(m_cmdBuf, m_wfCntBuf.buf, 8, 4, 0);
                memBarrier(vk::VK_PIPELINE_STAGE_TRANSFER_BIT, vk::VK_ACCESS_TRANSFER_WRITE_BIT,
                           vk::VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           vk::VK_ACCESS_SHADER_READ_BIT | vk::VK_ACCESS_SHADER_WRITE_BIT);

                // Stage 1: Intersect (RT veya Compute BVH2)
                if (wfUseRt) {
                    m_vk.cmdBindPipeline(m_cmdBuf, vk::VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeWfIntersectRt);
                    m_vk.cmdBindDescriptorSets(m_cmdBuf, vk::VK_PIPELINE_BIND_POINT_COMPUTE, m_plWfRt, 0, 1, &m_dsWfRt, 0, nullptr);
                    m_vk.cmdPushConstants(m_cmdBuf, m_plWfRt, vk::VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, pc);
                } else {
                    m_vk.cmdBindPipeline(m_cmdBuf, vk::VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeWfIntersect);
                    m_vk.cmdBindDescriptorSets(m_cmdBuf, vk::VK_PIPELINE_BIND_POINT_COMPUTE, m_plWf, 0, 1, &m_dsWf, 0, nullptr);
                    m_vk.cmdPushConstants(m_cmdBuf, m_plWf, vk::VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, pc);
                }
                m_vk.cmdDispatch(m_cmdBuf, groups, 1, 1);
                memBarrier(vk::VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, vk::VK_ACCESS_SHADER_WRITE_BIT,
                           vk::VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           vk::VK_ACCESS_SHADER_READ_BIT | vk::VK_ACCESS_SHADER_WRITE_BIT);

                // Stage 2: Shade
                m_vk.cmdBindPipeline(m_cmdBuf, vk::VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeWfShade);
                m_vk.cmdBindDescriptorSets(m_cmdBuf, vk::VK_PIPELINE_BIND_POINT_COMPUTE, m_plWf, 0, 1, &m_dsWf, 0, nullptr);
                m_vk.cmdPushConstants(m_cmdBuf, m_plWf, vk::VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, pc);
                m_vk.cmdDispatch(m_cmdBuf, groups, 1, 1);
                memBarrier(vk::VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, vk::VK_ACCESS_SHADER_WRITE_BIT,
                           vk::VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           vk::VK_ACCESS_SHADER_READ_BIT | vk::VK_ACCESS_SHADER_WRITE_BIT);

                // Stage 3: Shadow
                m_vk.cmdBindPipeline(m_cmdBuf, vk::VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeWfShadow);
                m_vk.cmdBindDescriptorSets(m_cmdBuf, vk::VK_PIPELINE_BIND_POINT_COMPUTE, m_plWf, 0, 1, &m_dsWf, 0, nullptr);
                m_vk.cmdPushConstants(m_cmdBuf, m_plWf, vk::VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, pc);
                m_vk.cmdDispatch(m_cmdBuf, groups, 1, 1);
                memBarrier(vk::VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, vk::VK_ACCESS_SHADER_WRITE_BIT,
                           vk::VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           vk::VK_ACCESS_SHADER_READ_BIT | vk::VK_ACCESS_SHADER_WRITE_BIT);
            }
        }
    } else {
        const uint32_t gx = (static_cast<uint32_t>(w) + 7u) / 8u;
        const uint32_t gy = (static_cast<uint32_t>(h) + 7u) / 8u;
        if (useRt) {
            m_vk.cmdBindPipeline(m_cmdBuf, vk::VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeRt);
            m_vk.cmdBindDescriptorSets(m_cmdBuf, vk::VK_PIPELINE_BIND_POINT_COMPUTE, m_plRt, 0, 1, &m_dsRt, 0, nullptr);
        } else {
            m_vk.cmdBindPipeline(m_cmdBuf, vk::VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeCompute);
            m_vk.cmdBindDescriptorSets(m_cmdBuf, vk::VK_PIPELINE_BIND_POINT_COMPUTE, m_plCompute, 0, 1, &m_dsCompute, 0, nullptr);
        }
        m_vk.cmdDispatch(m_cmdBuf, gx, gy, 1);
    }

    memBarrier(vk::VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, vk::VK_ACCESS_SHADER_WRITE_BIT,
               vk::VK_PIPELINE_STAGE_HOST_BIT, vk::VK_ACCESS_HOST_READ_BIT);
    m_vk.endCommandBuffer(m_cmdBuf);

    if (!submitAndWait(m_cmdBuf)) return SampleBatchResult{false, 0};

    // Geri okuma: Beauty + AOV + LightPass + LightGroups + Cryptomatte
    const auto* beautyPtr = static_cast<const gpu::Vec4*>(m_outBeautyBuf.mapped);
    const auto* aovPtr = wantAovs ? static_cast<const gpu::Vec4*>(m_outAovBuf.mapped) : nullptr;
    const auto* passPtr = wantPasses ? static_cast<const gpu::Vec4*>(m_outPassBuf.mapped) : nullptr;
    const uint32_t cnt = static_cast<uint32_t>(count);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
            const gpu::Vec4& b = beautyPtr[idx];
            targets.beauty->addBatch(x, y, Color3f(b.x, b.y, b.z), cnt, false);

            if (wantAovs) {
                const gpu::Vec4& a0 = aovPtr[idx * 3 + 0];
                const gpu::Vec4& a1 = aovPtr[idx * 3 + 1];
                targets.aovs->albedo.addBatch(x, y, Color3f(a0.x, a0.y, a0.z), cnt, false);
                targets.aovs->alpha.addBatch(x, y, Color3f(a0.w), cnt, false);
                targets.aovs->normal.addBatch(x, y, Color3f(a1.x, a1.y, a1.z), cnt, true);
            }
            if (wantPasses) {
                const size_t base = idx * gpu::kGpuPassVec4Stride;
                for (int k = 0; k < static_cast<int>(LightPass::Count); ++k) {
                    const gpu::Vec4& lp = passPtr[base + static_cast<size_t>(k)];
                    targets.passes->light[k].addBatch(x, y, Color3f(lp.x, lp.y, lp.z), cnt, false);
                }
                const size_t nGroups = std::min<size_t>(targets.passes->lightGroups.size(), 16u);
                for (size_t g = 0; g < nGroups; ++g) {
                    const gpu::Vec4& lg = passPtr[base + 8u + g];
                    targets.passes->lightGroups[g].second.addBatch(x, y, Color3f(lg.x, lg.y, lg.z), cnt, false);
                }
                if (wantAovs) {
                    const gpu::Vec4& a1 = aovPtr[idx * 3 + 1];
                    const gpu::Vec4& a2 = aovPtr[idx * 3 + 2];
                    targets.passes->depth.addBatch(x, y, Color3f(a1.w), cnt, false);
                    targets.passes->position.addBatch(x, y, Color3f(a2.x, a2.y, a2.z), cnt, true);
                }
                const gpu::Vec4& oid = passPtr[base + 24u];
                targets.passes->objectId.addBatch(x, y, Color3f(oid.x, oid.y, oid.z), cnt, false);
                targets.passes->materialId.addBatch(x, y, idColor(f32ToU32(oid.w)) * static_cast<float>(cnt), cnt, false);

                // Cryptomatte 4-girişli anti-aliased kapsama geri okuma
                targets.passes->cryptoObject.addTotalSamples(x, y, cnt);
                targets.passes->cryptoMaterial.addTotalSamples(x, y, cnt);
                auto readCrypto = [&](size_t slot, CryptoFilm& cf) {
                    const gpu::Vec4& v0 = passPtr[slot];
                    const gpu::Vec4& v1 = passPtr[slot + 1u];
                    const float pairs[4][2] = {{v0.x, v0.y}, {v0.z, v0.w}, {v1.x, v1.y}, {v1.z, v1.w}};
                    for (int k = 0; k < 4; ++k) {
                        const uint32_t c = static_cast<uint32_t>(std::lround(std::max(0.0f, pairs[k][1])));
                        if (c > 0 && pairs[k][0] != 0.0f) cf.addSamples(x, y, pairs[k][0], c);
                    }
                };
                readCrypto(base + 25u, targets.passes->cryptoObject);
                readCrypto(base + 27u, targets.passes->cryptoMaterial);
            }
        }
    }

    res.completed = true;
    res.samplesTraced = static_cast<uint64_t>(w) * static_cast<uint64_t>(h) * static_cast<uint64_t>(count);
    return res;
}

} // namespace photon
