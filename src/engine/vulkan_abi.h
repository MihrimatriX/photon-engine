// vulkan_abi.h — Sıfır Vulkan SDK bağımlılığıyla Khronos Vulkan 1.0/1.1/1.2 + Donanım RT
// (VK_KHR_buffer_device_address, VK_KHR_acceleration_structure, VK_KHR_ray_query) ABI
// tanımları ve dinamik çalışma zamanı yükleyicisi (dlopen / LoadLibraryExA).
//
// Derleme zamanında Vulkan SDK veya vulkan.h gerekmez; Windows (vulkan-1.dll) ve Linux
// (libvulkan.so.1) üzerinde sürücü varsa yüklenir, yoksa zarifçe boş cihaz listesi döner.
#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32) && !defined(_WIN64)
#define PHOTON_VKAPI __stdcall
#else
#define PHOTON_VKAPI
#endif

namespace photon::vk {

// ── Temel skalerler ve tutamaklar ──
using VkFlags = uint32_t;
using VkBool32 = uint32_t;
using VkDeviceSize = uint64_t;
using VkDeviceAddress = uint64_t;
using VkSampleMask = uint32_t;

constexpr VkBool32 VK_FALSE = 0;
constexpr VkBool32 VK_TRUE = 1;
constexpr uint32_t VK_QUEUE_FAMILY_IGNORED = ~0u;
constexpr VkDeviceSize VK_WHOLE_SIZE = ~0ull;
constexpr uint32_t VK_MAX_PHYSICAL_DEVICE_NAME_SIZE = 256;
constexpr uint32_t VK_UUID_SIZE = 16;
constexpr uint32_t VK_MAX_MEMORY_TYPES = 32;
constexpr uint32_t VK_MAX_MEMORY_HEAPS = 16;
constexpr uint32_t VK_MAX_EXTENSION_NAME_SIZE = 256;

constexpr uint32_t makeApiVersion(uint32_t variant, uint32_t major, uint32_t minor, uint32_t patch) {
    return (variant << 29) | (major << 22) | (minor << 12) | patch;
}
constexpr uint32_t apiVersionMajor(uint32_t v) { return (v >> 22) & 0x7Fu; }
constexpr uint32_t apiVersionMinor(uint32_t v) { return (v >> 12) & 0x3FFu; }
constexpr uint32_t apiVersionPatch(uint32_t v) { return v & 0xFFFu; }

constexpr uint32_t VK_API_VERSION_1_1 = makeApiVersion(0, 1, 1, 0);
constexpr uint32_t VK_API_VERSION_1_2 = makeApiVersion(0, 1, 2, 0);

// Dispatchable tutamaklar (işaretçi boyutunda)
struct VkInstance_T;
struct VkPhysicalDevice_T;
struct VkDevice_T;
struct VkQueue_T;
struct VkCommandBuffer_T;
using VkInstance = VkInstance_T*;
using VkPhysicalDevice = VkPhysicalDevice_T*;
using VkDevice = VkDevice_T*;
using VkQueue = VkQueue_T*;
using VkCommandBuffer = VkCommandBuffer_T*;

// Non-dispatchable tutamaklar (tüm platformlarda 64-bit)
using VkDeviceMemory = uint64_t;
using VkBuffer = uint64_t;
using VkShaderModule = uint64_t;
using VkPipeline = uint64_t;
using VkPipelineLayout = uint64_t;
using VkPipelineCache = uint64_t;
using VkDescriptorSetLayout = uint64_t;
using VkDescriptorPool = uint64_t;
using VkDescriptorSet = uint64_t;
using VkCommandPool = uint64_t;
using VkFence = uint64_t;
using VkSemaphore = uint64_t;
using VkSampler = uint64_t;
using VkImageView = uint64_t;
using VkBufferView = uint64_t;
using VkAccelerationStructureKHR = uint64_t;

constexpr uint64_t VK_NULL_HANDLE = 0ull;

// ── Sonuç kodları ──
enum VkResult : int32_t {
    VK_SUCCESS = 0,
    VK_NOT_READY = 1,
    VK_TIMEOUT = 2,
    VK_INCOMPLETE = 5,
    VK_ERROR_OUT_OF_HOST_MEMORY = -1,
    VK_ERROR_OUT_OF_DEVICE_MEMORY = -2,
    VK_ERROR_INITIALIZATION_FAILED = -3,
    VK_ERROR_DEVICE_LOST = -4,
    VK_ERROR_EXTENSION_NOT_PRESENT = -7,
    VK_ERROR_FEATURE_NOT_PRESENT = -8,
};

// ── Yapı türleri (Khronos vk_structure_type tam değerleri) ──
enum VkStructureType : int32_t {
    VK_STRUCTURE_TYPE_APPLICATION_INFO = 0,
    VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1,
    VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO = 2,
    VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO = 3,
    VK_STRUCTURE_TYPE_SUBMIT_INFO = 4,
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO = 5,
    VK_STRUCTURE_TYPE_FENCE_CREATE_INFO = 8,
    VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO = 12,
    VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO = 16,
    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO = 18,
    VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO = 29,
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO = 30,
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO = 32,
    VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO = 33,
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO = 34,
    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET = 35,
    VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO = 39,
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO = 40,
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO = 42,
    VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER = 44,
    VK_STRUCTURE_TYPE_MEMORY_BARRIER = 46,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 = 1000059000,
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO = 1000060000,
    VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO = 1000244001,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES = 1000257000,
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR = 1000150000,
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR = 1000150002,
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR = 1000150004,
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR = 1000150005,
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR = 1000150006,
    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR = 1000150007,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR = 1000150013,
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR = 1000150017,
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR = 1000150020,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR = 1000348013,
};

enum VkPhysicalDeviceType : int32_t {
    VK_PHYSICAL_DEVICE_TYPE_OTHER = 0,
    VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU = 1,
    VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU = 2,
    VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU = 3,
    VK_PHYSICAL_DEVICE_TYPE_CPU = 4,
};

enum VkSharingMode : int32_t {
    VK_SHARING_MODE_EXCLUSIVE = 0,
    VK_SHARING_MODE_CONCURRENT = 1,
};

enum VkDescriptorType : int32_t {
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER = 6,
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER = 7,
    VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR = 1000150000,
};

enum VkCommandBufferLevel : int32_t {
    VK_COMMAND_BUFFER_LEVEL_PRIMARY = 0,
};

enum VkPipelineBindPoint : int32_t {
    VK_PIPELINE_BIND_POINT_COMPUTE = 1,
};

enum VkFormat : int32_t {
    VK_FORMAT_R32G32B32_SFLOAT = 106,
};

enum VkIndexType : int32_t {
    VK_INDEX_TYPE_UINT16 = 0,
    VK_INDEX_TYPE_UINT32 = 1,
    VK_INDEX_TYPE_NONE_KHR = 1000165000,
};

enum VkGeometryTypeKHR : int32_t {
    VK_GEOMETRY_TYPE_TRIANGLES_KHR = 0,
    VK_GEOMETRY_TYPE_AABBS_KHR = 1,
    VK_GEOMETRY_TYPE_INSTANCES_KHR = 2,
};

enum VkAccelerationStructureTypeKHR : int32_t {
    VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR = 0,
    VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR = 1,
};

enum VkBuildAccelerationStructureModeKHR : int32_t {
    VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR = 0,
    VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR = 1,
};

enum VkAccelerationStructureBuildTypeKHR : int32_t {
    VK_ACCELERATION_STRUCTURE_BUILD_TYPE_HOST_KHR = 0,
    VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR = 1,
};

// ── Bayrak sabitleri ──
constexpr VkFlags VK_QUEUE_COMPUTE_BIT = 0x00000002u;
constexpr VkFlags VK_QUEUE_TRANSFER_BIT = 0x00000004u;

constexpr VkFlags VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT = 0x00000001u;
constexpr VkFlags VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT = 0x00000002u;
constexpr VkFlags VK_MEMORY_PROPERTY_HOST_COHERENT_BIT = 0x00000004u;
constexpr VkFlags VK_MEMORY_PROPERTY_HOST_CACHED_BIT = 0x00000008u;
constexpr VkFlags VK_MEMORY_HEAP_DEVICE_LOCAL_BIT = 0x00000001u;

constexpr VkFlags VK_BUFFER_USAGE_TRANSFER_SRC_BIT = 0x00000001u;
constexpr VkFlags VK_BUFFER_USAGE_TRANSFER_DST_BIT = 0x00000002u;
constexpr VkFlags VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT = 0x00000010u;
constexpr VkFlags VK_BUFFER_USAGE_STORAGE_BUFFER_BIT = 0x00000020u;
constexpr VkFlags VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT = 0x00020000u;
constexpr VkFlags VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR = 0x00080000u;
constexpr VkFlags VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR = 0x00100000u;

constexpr VkFlags VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT = 0x00000002u;
constexpr VkFlags VK_SHADER_STAGE_COMPUTE_BIT = 0x00000020u;
constexpr VkFlags VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT = 0x00000002u;
constexpr VkFlags VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT = 0x00000001u;
constexpr VkFlags VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT = 0x00000001u;

constexpr VkFlags VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT = 0x00000800u;
constexpr VkFlags VK_PIPELINE_STAGE_TRANSFER_BIT = 0x00001000u;
constexpr VkFlags VK_PIPELINE_STAGE_HOST_BIT = 0x00004000u;
constexpr VkFlags VK_PIPELINE_STAGE_ALL_COMMANDS_BIT = 0x00010000u;
constexpr VkFlags VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR = 0x02000000u;

constexpr VkFlags VK_ACCESS_SHADER_READ_BIT = 0x00000020u;
constexpr VkFlags VK_ACCESS_SHADER_WRITE_BIT = 0x00000040u;
constexpr VkFlags VK_ACCESS_TRANSFER_READ_BIT = 0x00000800u;
constexpr VkFlags VK_ACCESS_TRANSFER_WRITE_BIT = 0x00001000u;
constexpr VkFlags VK_ACCESS_HOST_READ_BIT = 0x00002000u;
constexpr VkFlags VK_ACCESS_HOST_WRITE_BIT = 0x00004000u;
constexpr VkFlags VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR = 0x00200000u;
constexpr VkFlags VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR = 0x00400000u;

constexpr VkFlags VK_GEOMETRY_OPAQUE_BIT_KHR = 0x00000001u;
constexpr VkFlags VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR = 0x00000004u;
constexpr VkFlags VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR = 0x00000001u;

// ── Yapılar (Khronos C ABI düzeniyle birebir) ──

struct VkApplicationInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    const void* pNext = nullptr;
    const char* pApplicationName = nullptr;
    uint32_t applicationVersion = 0;
    const char* pEngineName = nullptr;
    uint32_t engineVersion = 0;
    uint32_t apiVersion = VK_API_VERSION_1_1;
};

struct VkInstanceCreateInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    const VkApplicationInfo* pApplicationInfo = nullptr;
    uint32_t enabledLayerCount = 0;
    const char* const* ppEnabledLayerNames = nullptr;
    uint32_t enabledExtensionCount = 0;
    const char* const* ppEnabledExtensionNames = nullptr;
};

struct VkExtensionProperties {
    char extensionName[VK_MAX_EXTENSION_NAME_SIZE];
    uint32_t specVersion;
};
static_assert(sizeof(VkExtensionProperties) == 260, "VkExtensionProperties ABI");

struct VkPhysicalDeviceProperties {
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    VkPhysicalDeviceType deviceType;
    char deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    uint8_t pipelineCacheUUID[VK_UUID_SIZE];
    uint8_t limitsAndSparse[768]; // VkPhysicalDeviceLimits (504) + Sparse (20) + güvenlik payı
};

struct VkQueueFamilyProperties {
    VkFlags queueFlags;
    uint32_t queueCount;
    uint32_t timestampValidBits;
    uint32_t minImageTransferGranularity[3];
};
static_assert(sizeof(VkQueueFamilyProperties) == 24, "VkQueueFamilyProperties ABI");

struct VkMemoryType {
    VkFlags propertyFlags;
    uint32_t heapIndex;
};
static_assert(sizeof(VkMemoryType) == 8, "VkMemoryType ABI");

struct VkMemoryHeap {
    VkDeviceSize size;
    VkFlags flags;
};
static_assert(sizeof(VkMemoryHeap) == 16, "VkMemoryHeap ABI");

struct VkPhysicalDeviceMemoryProperties {
    uint32_t memoryTypeCount;
    VkMemoryType memoryTypes[VK_MAX_MEMORY_TYPES];
    uint32_t memoryHeapCount;
    VkMemoryHeap memoryHeaps[VK_MAX_MEMORY_HEAPS];
};
static_assert(sizeof(VkPhysicalDeviceMemoryProperties) == 520, "VkPhysicalDeviceMemoryProperties ABI");

struct VkPhysicalDeviceFeatures {
    VkBool32 fields[55];
};
static_assert(sizeof(VkPhysicalDeviceFeatures) == 220, "VkPhysicalDeviceFeatures ABI");

struct VkPhysicalDeviceFeatures2 {
    VkStructureType sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    void* pNext = nullptr;
    VkPhysicalDeviceFeatures features{};
};

struct VkPhysicalDeviceBufferDeviceAddressFeatures {
    VkStructureType sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    void* pNext = nullptr;
    VkBool32 bufferDeviceAddress = VK_FALSE;
    VkBool32 bufferDeviceAddressCaptureReplay = VK_FALSE;
    VkBool32 bufferDeviceAddressMultiDevice = VK_FALSE;
};

struct VkPhysicalDeviceAccelerationStructureFeaturesKHR {
    VkStructureType sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    void* pNext = nullptr;
    VkBool32 accelerationStructure = VK_FALSE;
    VkBool32 accelerationStructureCaptureReplay = VK_FALSE;
    VkBool32 accelerationStructureIndirectBuild = VK_FALSE;
    VkBool32 accelerationStructureHostCommands = VK_FALSE;
    VkBool32 descriptorBindingAccelerationStructureUpdateAfterBind = VK_FALSE;
};

struct VkPhysicalDeviceRayQueryFeaturesKHR {
    VkStructureType sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    void* pNext = nullptr;
    VkBool32 rayQuery = VK_FALSE;
};

struct VkDeviceQueueCreateInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    uint32_t queueFamilyIndex = 0;
    uint32_t queueCount = 1;
    const float* pQueuePriorities = nullptr;
};

struct VkDeviceCreateInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    uint32_t queueCreateInfoCount = 0;
    const VkDeviceQueueCreateInfo* pQueueCreateInfos = nullptr;
    uint32_t enabledLayerCount = 0;
    const char* const* ppEnabledLayerNames = nullptr;
    uint32_t enabledExtensionCount = 0;
    const char* const* ppEnabledExtensionNames = nullptr;
    const VkPhysicalDeviceFeatures* pEnabledFeatures = nullptr;
};

struct VkBufferCreateInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    VkDeviceSize size = 0;
    VkFlags usage = 0;
    VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    uint32_t queueFamilyIndexCount = 0;
    const uint32_t* pQueueFamilyIndices = nullptr;
};

struct VkMemoryRequirements {
    VkDeviceSize size;
    VkDeviceSize alignment;
    uint32_t memoryTypeBits;
};

struct VkMemoryAllocateFlagsInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    uint32_t deviceMask = 0;
};

struct VkMemoryAllocateInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    const void* pNext = nullptr;
    VkDeviceSize allocationSize = 0;
    uint32_t memoryTypeIndex = 0;
};

struct VkBufferDeviceAddressInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    const void* pNext = nullptr;
    VkBuffer buffer = VK_NULL_HANDLE;
};

struct VkBufferCopy {
    VkDeviceSize srcOffset = 0;
    VkDeviceSize dstOffset = 0;
    VkDeviceSize size = 0;
};

struct VkShaderModuleCreateInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    size_t codeSize = 0;
    const uint32_t* pCode = nullptr;
};

struct VkDescriptorSetLayoutBinding {
    uint32_t binding = 0;
    VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    uint32_t descriptorCount = 1;
    VkFlags stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    const VkSampler* pImmutableSamplers = nullptr;
};

struct VkDescriptorSetLayoutCreateInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    uint32_t bindingCount = 0;
    const VkDescriptorSetLayoutBinding* pBindings = nullptr;
};

struct VkPushConstantRange {
    VkFlags stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    uint32_t offset = 0;
    uint32_t size = 0;
};

struct VkPipelineLayoutCreateInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    uint32_t setLayoutCount = 0;
    const VkDescriptorSetLayout* pSetLayouts = nullptr;
    uint32_t pushConstantRangeCount = 0;
    const VkPushConstantRange* pPushConstantRanges = nullptr;
};

struct VkPipelineShaderStageCreateInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    VkFlags stage = VK_SHADER_STAGE_COMPUTE_BIT;
    VkShaderModule module = VK_NULL_HANDLE;
    const char* pName = "main";
    const void* pSpecializationInfo = nullptr;
};

struct VkComputePipelineCreateInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    VkPipelineShaderStageCreateInfo stage{};
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline basePipelineHandle = VK_NULL_HANDLE;
    int32_t basePipelineIndex = -1;
};

struct VkDescriptorPoolSize {
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    uint32_t descriptorCount = 0;
};

struct VkDescriptorPoolCreateInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    uint32_t maxSets = 0;
    uint32_t poolSizeCount = 0;
    const VkDescriptorPoolSize* pPoolSizes = nullptr;
};

struct VkDescriptorSetAllocateInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    const void* pNext = nullptr;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    uint32_t descriptorSetCount = 0;
    const VkDescriptorSetLayout* pSetLayouts = nullptr;
};

struct VkDescriptorBufferInfo {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize range = VK_WHOLE_SIZE;
};

struct VkWriteDescriptorSet {
    VkStructureType sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    const void* pNext = nullptr;
    VkDescriptorSet dstSet = VK_NULL_HANDLE;
    uint32_t dstBinding = 0;
    uint32_t dstArrayElement = 0;
    uint32_t descriptorCount = 1;
    VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    const void* pImageInfo = nullptr;
    const VkDescriptorBufferInfo* pBufferInfo = nullptr;
    const VkBufferView* pTexelBufferView = nullptr;
};

struct VkWriteDescriptorSetAccelerationStructureKHR {
    VkStructureType sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    const void* pNext = nullptr;
    uint32_t accelerationStructureCount = 0;
    const VkAccelerationStructureKHR* pAccelerationStructures = nullptr;
};

struct VkCommandPoolCreateInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    uint32_t queueFamilyIndex = 0;
};

struct VkCommandBufferAllocateInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    const void* pNext = nullptr;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    uint32_t commandBufferCount = 1;
};

struct VkCommandBufferBeginInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    const void* pInheritanceInfo = nullptr;
};

struct VkMemoryBarrier {
    VkStructureType sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    const void* pNext = nullptr;
    VkFlags srcAccessMask = 0;
    VkFlags dstAccessMask = 0;
};

struct VkBufferMemoryBarrier {
    VkStructureType sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    const void* pNext = nullptr;
    VkFlags srcAccessMask = 0;
    VkFlags dstAccessMask = 0;
    uint32_t srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    uint32_t dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = VK_WHOLE_SIZE;
};

struct VkSubmitInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    const void* pNext = nullptr;
    uint32_t waitSemaphoreCount = 0;
    const VkSemaphore* pWaitSemaphores = nullptr;
    const VkFlags* pWaitDstStageMask = nullptr;
    uint32_t commandBufferCount = 0;
    const VkCommandBuffer* pCommandBuffers = nullptr;
    uint32_t signalSemaphoreCount = 0;
    const VkSemaphore* pSignalSemaphores = nullptr;
};

struct VkFenceCreateInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    const void* pNext = nullptr;
    VkFlags flags = 0;
};

// ── VK_KHR_acceleration_structure yapıları ──

union VkDeviceOrHostAddressKHR {
    VkDeviceAddress deviceAddress;
    void* hostAddress;
};

union VkDeviceOrHostAddressConstKHR {
    VkDeviceAddress deviceAddress;
    const void* hostAddress;
};

struct VkTransformMatrixKHR {
    float matrix[3][4];
};
static_assert(sizeof(VkTransformMatrixKHR) == 48, "VkTransformMatrixKHR ABI");

struct VkAccelerationStructureInstanceKHR {
    VkTransformMatrixKHR transform;
    uint32_t instanceCustomIndexAndMask; ///< bit 0..23: customIndex, bit 24..31: mask
    uint32_t sbtOffsetAndFlags;          ///< bit 0..23: sbtOffset, bit 24..31: flags
    uint64_t accelerationStructureReference;
};
static_assert(sizeof(VkAccelerationStructureInstanceKHR) == 64, "VkAccelerationStructureInstanceKHR ABI must be 64 bytes");

struct VkAccelerationStructureGeometryTrianglesDataKHR {
    VkStructureType sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    const void* pNext = nullptr;
    VkFormat vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    VkDeviceOrHostAddressConstKHR vertexData{0};
    VkDeviceSize vertexStride = 12;
    uint32_t maxVertex = 0;
    VkIndexType indexType = VK_INDEX_TYPE_NONE_KHR;
    VkDeviceOrHostAddressConstKHR indexData{0};
    VkDeviceOrHostAddressConstKHR transformData{0};
};
static_assert(sizeof(VkAccelerationStructureGeometryTrianglesDataKHR) == 64, "TrianglesData ABI");

struct VkAccelerationStructureGeometryAabbsDataKHR {
    VkStructureType sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR; // kullanılmıyor
    const void* pNext = nullptr;
    VkDeviceOrHostAddressConstKHR data{0};
    VkDeviceSize stride = 0;
};

struct VkAccelerationStructureGeometryInstancesDataKHR {
    VkStructureType sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    const void* pNext = nullptr;
    VkBool32 arrayOfPointers = VK_FALSE;
    VkDeviceOrHostAddressConstKHR data{0};
};

union VkAccelerationStructureGeometryDataKHR {
    VkAccelerationStructureGeometryTrianglesDataKHR triangles;
    VkAccelerationStructureGeometryAabbsDataKHR aabbs;
    VkAccelerationStructureGeometryInstancesDataKHR instances;
    VkAccelerationStructureGeometryDataKHR() : triangles{} {}
};

struct VkAccelerationStructureGeometryKHR {
    VkStructureType sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    const void* pNext = nullptr;
    VkGeometryTypeKHR geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    VkAccelerationStructureGeometryDataKHR geometry{};
    VkFlags flags = 0;
};
static_assert(sizeof(VkAccelerationStructureGeometryKHR) == 96, "GeometryKHR ABI");

struct VkAccelerationStructureBuildGeometryInfoKHR {
    VkStructureType sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    const void* pNext = nullptr;
    VkAccelerationStructureTypeKHR type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    VkFlags flags = 0;
    VkBuildAccelerationStructureModeKHR mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    VkAccelerationStructureKHR srcAccelerationStructure = VK_NULL_HANDLE;
    VkAccelerationStructureKHR dstAccelerationStructure = VK_NULL_HANDLE;
    uint32_t geometryCount = 0;
    const VkAccelerationStructureGeometryKHR* pGeometries = nullptr;
    const VkAccelerationStructureGeometryKHR* const* ppGeometries = nullptr;
    VkDeviceOrHostAddressKHR scratchData{0};
};

struct VkAccelerationStructureBuildSizesInfoKHR {
    VkStructureType sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    const void* pNext = nullptr;
    VkDeviceSize accelerationStructureSize = 0;
    VkDeviceSize updateScratchSize = 0;
    VkDeviceSize buildScratchSize = 0;
};

struct VkAccelerationStructureCreateInfoKHR {
    VkStructureType sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    const void* pNext = nullptr;
    VkFlags createFlags = 0;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    VkAccelerationStructureTypeKHR type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    VkDeviceAddress deviceAddress = 0;
};

struct VkAccelerationStructureBuildRangeInfoKHR {
    uint32_t primitiveCount = 0;
    uint32_t primitiveOffset = 0;
    uint32_t firstVertex = 0;
    uint32_t transformOffset = 0;
};
static_assert(sizeof(VkAccelerationStructureBuildRangeInfoKHR) == 16, "BuildRangeInfoKHR ABI");

struct VkAccelerationStructureDeviceAddressInfoKHR {
    VkStructureType sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    const void* pNext = nullptr;
    VkAccelerationStructureKHR accelerationStructure = VK_NULL_HANDLE;
};

// ── İşlev işaretçileri ──
using PFN_vkVoidFunction = void(PHOTON_VKAPI*)();
using PFN_vkGetInstanceProcAddr = PFN_vkVoidFunction(PHOTON_VKAPI*)(VkInstance, const char*);
using PFN_vkGetDeviceProcAddr = PFN_vkVoidFunction(PHOTON_VKAPI*)(VkDevice, const char*);
using PFN_vkEnumerateInstanceVersion = VkResult(PHOTON_VKAPI*)(uint32_t*);
using PFN_vkCreateInstance = VkResult(PHOTON_VKAPI*)(const VkInstanceCreateInfo*, const void*, VkInstance*);
using PFN_vkDestroyInstance = void(PHOTON_VKAPI*)(VkInstance, const void*);
using PFN_vkEnumeratePhysicalDevices = VkResult(PHOTON_VKAPI*)(VkInstance, uint32_t*, VkPhysicalDevice*);
using PFN_vkGetPhysicalDeviceProperties = void(PHOTON_VKAPI*)(VkPhysicalDevice, VkPhysicalDeviceProperties*);
using PFN_vkGetPhysicalDeviceMemoryProperties = void(PHOTON_VKAPI*)(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties*);
using PFN_vkGetPhysicalDeviceQueueFamilyProperties = void(PHOTON_VKAPI*)(VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties*);
using PFN_vkEnumerateDeviceExtensionProperties = VkResult(PHOTON_VKAPI*)(VkPhysicalDevice, const char*, uint32_t*, VkExtensionProperties*);
using PFN_vkGetPhysicalDeviceFeatures2 = void(PHOTON_VKAPI*)(VkPhysicalDevice, VkPhysicalDeviceFeatures2*);
using PFN_vkCreateDevice = VkResult(PHOTON_VKAPI*)(VkPhysicalDevice, const VkDeviceCreateInfo*, const void*, VkDevice*);
using PFN_vkDestroyDevice = void(PHOTON_VKAPI*)(VkDevice, const void*);
using PFN_vkGetDeviceQueue = void(PHOTON_VKAPI*)(VkDevice, uint32_t, uint32_t, VkQueue*);
using PFN_vkDeviceWaitIdle = VkResult(PHOTON_VKAPI*)(VkDevice);
using PFN_vkCreateBuffer = VkResult(PHOTON_VKAPI*)(VkDevice, const VkBufferCreateInfo*, const void*, VkBuffer*);
using PFN_vkDestroyBuffer = void(PHOTON_VKAPI*)(VkDevice, VkBuffer, const void*);
using PFN_vkGetBufferMemoryRequirements = void(PHOTON_VKAPI*)(VkDevice, VkBuffer, VkMemoryRequirements*);
using PFN_vkAllocateMemory = VkResult(PHOTON_VKAPI*)(VkDevice, const VkMemoryAllocateInfo*, const void*, VkDeviceMemory*);
using PFN_vkFreeMemory = void(PHOTON_VKAPI*)(VkDevice, VkDeviceMemory, const void*);
using PFN_vkBindBufferMemory = VkResult(PHOTON_VKAPI*)(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize);
using PFN_vkMapMemory = VkResult(PHOTON_VKAPI*)(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkFlags, void**);
using PFN_vkUnmapMemory = void(PHOTON_VKAPI*)(VkDevice, VkDeviceMemory);
using PFN_vkCreateShaderModule = VkResult(PHOTON_VKAPI*)(VkDevice, const VkShaderModuleCreateInfo*, const void*, VkShaderModule*);
using PFN_vkDestroyShaderModule = void(PHOTON_VKAPI*)(VkDevice, VkShaderModule, const void*);
using PFN_vkCreateDescriptorSetLayout = VkResult(PHOTON_VKAPI*)(VkDevice, const VkDescriptorSetLayoutCreateInfo*, const void*, VkDescriptorSetLayout*);
using PFN_vkDestroyDescriptorSetLayout = void(PHOTON_VKAPI*)(VkDevice, VkDescriptorSetLayout, const void*);
using PFN_vkCreatePipelineLayout = VkResult(PHOTON_VKAPI*)(VkDevice, const VkPipelineLayoutCreateInfo*, const void*, VkPipelineLayout*);
using PFN_vkDestroyPipelineLayout = void(PHOTON_VKAPI*)(VkDevice, VkPipelineLayout, const void*);
using PFN_vkCreateComputePipelines = VkResult(PHOTON_VKAPI*)(VkDevice, VkPipelineCache, uint32_t, const VkComputePipelineCreateInfo*, const void*, VkPipeline*);
using PFN_vkDestroyPipeline = void(PHOTON_VKAPI*)(VkDevice, VkPipeline, const void*);
using PFN_vkCreateDescriptorPool = VkResult(PHOTON_VKAPI*)(VkDevice, const VkDescriptorPoolCreateInfo*, const void*, VkDescriptorPool*);
using PFN_vkDestroyDescriptorPool = void(PHOTON_VKAPI*)(VkDevice, VkDescriptorPool, const void*);
using PFN_vkAllocateDescriptorSets = VkResult(PHOTON_VKAPI*)(VkDevice, const VkDescriptorSetAllocateInfo*, VkDescriptorSet*);
using PFN_vkUpdateDescriptorSets = void(PHOTON_VKAPI*)(VkDevice, uint32_t, const VkWriteDescriptorSet*, uint32_t, const void*);
using PFN_vkCreateCommandPool = VkResult(PHOTON_VKAPI*)(VkDevice, const VkCommandPoolCreateInfo*, const void*, VkCommandPool*);
using PFN_vkDestroyCommandPool = void(PHOTON_VKAPI*)(VkDevice, VkCommandPool, const void*);
using PFN_vkAllocateCommandBuffers = VkResult(PHOTON_VKAPI*)(VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer*);
using PFN_vkFreeCommandBuffers = void(PHOTON_VKAPI*)(VkDevice, VkCommandPool, uint32_t, const VkCommandBuffer*);
using PFN_vkResetCommandBuffer = VkResult(PHOTON_VKAPI*)(VkCommandBuffer, VkFlags);
using PFN_vkBeginCommandBuffer = VkResult(PHOTON_VKAPI*)(VkCommandBuffer, const VkCommandBufferBeginInfo*);
using PFN_vkEndCommandBuffer = VkResult(PHOTON_VKAPI*)(VkCommandBuffer);
using PFN_vkCmdBindPipeline = void(PHOTON_VKAPI*)(VkCommandBuffer, VkPipelineBindPoint, VkPipeline);
using PFN_vkCmdBindDescriptorSets = void(PHOTON_VKAPI*)(VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet*, uint32_t, const uint32_t*);
using PFN_vkCmdPushConstants = void(PHOTON_VKAPI*)(VkCommandBuffer, VkPipelineLayout, VkFlags, uint32_t, uint32_t, const void*);
using PFN_vkCmdDispatch = void(PHOTON_VKAPI*)(VkCommandBuffer, uint32_t, uint32_t, uint32_t);
using PFN_vkCmdCopyBuffer = void(PHOTON_VKAPI*)(VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy*);
using PFN_vkCmdFillBuffer = void(PHOTON_VKAPI*)(VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize, uint32_t);
using PFN_vkCmdPipelineBarrier = void(PHOTON_VKAPI*)(VkCommandBuffer, VkFlags, VkFlags, VkFlags, uint32_t, const VkMemoryBarrier*, uint32_t, const VkBufferMemoryBarrier*, uint32_t, const void*);
using PFN_vkQueueSubmit = VkResult(PHOTON_VKAPI*)(VkQueue, uint32_t, const VkSubmitInfo*, VkFence);
using PFN_vkCreateFence = VkResult(PHOTON_VKAPI*)(VkDevice, const VkFenceCreateInfo*, const void*, VkFence*);
using PFN_vkDestroyFence = void(PHOTON_VKAPI*)(VkDevice, VkFence, const void*);
using PFN_vkWaitForFences = VkResult(PHOTON_VKAPI*)(VkDevice, uint32_t, const VkFence*, VkBool32, uint64_t);
using PFN_vkResetFences = VkResult(PHOTON_VKAPI*)(VkDevice, uint32_t, const VkFence*);

// Vulkan 1.2 + RT uzantı işlevleri
using PFN_vkGetBufferDeviceAddress = VkDeviceAddress(PHOTON_VKAPI*)(VkDevice, const VkBufferDeviceAddressInfo*);
using PFN_vkCreateAccelerationStructureKHR = VkResult(PHOTON_VKAPI*)(VkDevice, const VkAccelerationStructureCreateInfoKHR*, const void*, VkAccelerationStructureKHR*);
using PFN_vkDestroyAccelerationStructureKHR = void(PHOTON_VKAPI*)(VkDevice, VkAccelerationStructureKHR, const void*);
using PFN_vkGetAccelerationStructureBuildSizesKHR = void(PHOTON_VKAPI*)(VkDevice, VkAccelerationStructureBuildTypeKHR, const VkAccelerationStructureBuildGeometryInfoKHR*, const uint32_t*, VkAccelerationStructureBuildSizesInfoKHR*);
using PFN_vkGetAccelerationStructureDeviceAddressKHR = VkDeviceAddress(PHOTON_VKAPI*)(VkDevice, const VkAccelerationStructureDeviceAddressInfoKHR*);
using PFN_vkCmdBuildAccelerationStructuresKHR = void(PHOTON_VKAPI*)(VkCommandBuffer, uint32_t, const VkAccelerationStructureBuildGeometryInfoKHR*, const VkAccelerationStructureBuildRangeInfoKHR* const*);

/// Dinamik olarak yüklenen Vulkan işlev tablosu.
struct VulkanApi {
    bool loaded = false;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;
    PFN_vkEnumerateInstanceVersion enumerateInstanceVersion = nullptr;
    PFN_vkCreateInstance createInstance = nullptr;
    PFN_vkDestroyInstance destroyInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceProperties getPhysicalDeviceProperties = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties getPhysicalDeviceMemoryProperties = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties getPhysicalDeviceQueueFamilyProperties = nullptr;
    PFN_vkEnumerateDeviceExtensionProperties enumerateDeviceExtensionProperties = nullptr;
    PFN_vkGetPhysicalDeviceFeatures2 getPhysicalDeviceFeatures2 = nullptr;
    PFN_vkCreateDevice createDevice = nullptr;
    PFN_vkDestroyDevice destroyDevice = nullptr;
    PFN_vkGetDeviceQueue getDeviceQueue = nullptr;
    PFN_vkDeviceWaitIdle deviceWaitIdle = nullptr;
    PFN_vkCreateBuffer createBuffer = nullptr;
    PFN_vkDestroyBuffer destroyBuffer = nullptr;
    PFN_vkGetBufferMemoryRequirements getBufferMemoryRequirements = nullptr;
    PFN_vkAllocateMemory allocateMemory = nullptr;
    PFN_vkFreeMemory freeMemory = nullptr;
    PFN_vkBindBufferMemory bindBufferMemory = nullptr;
    PFN_vkMapMemory mapMemory = nullptr;
    PFN_vkUnmapMemory unmapMemory = nullptr;
    PFN_vkCreateShaderModule createShaderModule = nullptr;
    PFN_vkDestroyShaderModule destroyShaderModule = nullptr;
    PFN_vkCreateDescriptorSetLayout createDescriptorSetLayout = nullptr;
    PFN_vkDestroyDescriptorSetLayout destroyDescriptorSetLayout = nullptr;
    PFN_vkCreatePipelineLayout createPipelineLayout = nullptr;
    PFN_vkDestroyPipelineLayout destroyPipelineLayout = nullptr;
    PFN_vkCreateComputePipelines createComputePipelines = nullptr;
    PFN_vkDestroyPipeline destroyPipeline = nullptr;
    PFN_vkCreateDescriptorPool createDescriptorPool = nullptr;
    PFN_vkDestroyDescriptorPool destroyDescriptorPool = nullptr;
    PFN_vkAllocateDescriptorSets allocateDescriptorSets = nullptr;
    PFN_vkUpdateDescriptorSets updateDescriptorSets = nullptr;
    PFN_vkCreateCommandPool createCommandPool = nullptr;
    PFN_vkDestroyCommandPool destroyCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers allocateCommandBuffers = nullptr;
    PFN_vkFreeCommandBuffers freeCommandBuffers = nullptr;
    PFN_vkResetCommandBuffer resetCommandBuffer = nullptr;
    PFN_vkBeginCommandBuffer beginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer endCommandBuffer = nullptr;
    PFN_vkCmdBindPipeline cmdBindPipeline = nullptr;
    PFN_vkCmdBindDescriptorSets cmdBindDescriptorSets = nullptr;
    PFN_vkCmdPushConstants cmdPushConstants = nullptr;
    PFN_vkCmdDispatch cmdDispatch = nullptr;
    PFN_vkCmdCopyBuffer cmdCopyBuffer = nullptr;
    PFN_vkCmdFillBuffer cmdFillBuffer = nullptr;
    PFN_vkCmdPipelineBarrier cmdPipelineBarrier = nullptr;
    PFN_vkQueueSubmit queueSubmit = nullptr;
    PFN_vkCreateFence createFence = nullptr;
    PFN_vkDestroyFence destroyFence = nullptr;
    PFN_vkWaitForFences waitForFences = nullptr;
    PFN_vkResetFences resetFences = nullptr;

    // İsteğe bağlı RT uzantı işlevleri (cihaz oluşturulduktan sonra yüklenir)
    PFN_vkGetBufferDeviceAddress getBufferDeviceAddress = nullptr;
    PFN_vkCreateAccelerationStructureKHR createAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR destroyAccelerationStructureKHR = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR getAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR getAccelerationStructureDeviceAddressKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR cmdBuildAccelerationStructuresKHR = nullptr;
};

/// Sistem Vulkan kitaplığını bir kez yükler (dlopen/LoadLibraryExA); başarısızsa loaded = false.
const VulkanApi& loadVulkanGlobal();

/// Oluşturulan bir VkInstance üzerinden örnek işlevlerini yükler.
bool loadVulkanInstance(VkInstance instance, VulkanApi& api);

/// Oluşturulan bir VkDevice üzerinden cihaz (ve varsa RT) işlevlerini yükler.
bool loadVulkanDevice(VkDevice device, VulkanApi& api, bool loadRayQuery);

} // namespace photon::vk
