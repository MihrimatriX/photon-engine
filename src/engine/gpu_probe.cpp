// gpu_probe.cpp — CUDA sürücü API'sinin dinamik yüklenmesi ve cihaz bilgisinin okunması.
#include "engine/gpu_probe.h"

#include <cstring>
#include <sstream>

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

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

namespace photon {

namespace {

// CUDA sürücü API'sinden kullanılan küçük alt küme (cuda.h'ye gerek yok: imzalar kararlı).
using CUresult = int;
using CUdevice = int;
constexpr int kAttrComputeMajor = 75; // CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR
constexpr int kAttrComputeMinor = 76; // CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR
using FnInit = CUresult (*)(unsigned int);
using FnDriverVersion = CUresult (*)(int*);
using FnGetCount = CUresult (*)(int*);
using FnGet = CUresult (*)(CUdevice*, int);
using FnGetName = CUresult (*)(char*, int, CUdevice);
using FnTotalMem = CUresult (*)(size_t*, CUdevice);
using FnGetAttribute = CUresult (*)(int*, int, CUdevice);

/// Sürücü kitaplığı süreç boyunca yüklü kalır (bilerek boşaltılmaz): cuInit'ten sonra
/// boşaltmak, sürücünün kaydettiği çıkış işleyicilerini boşaltılmış koda yönlendirebilir.
class Library {
public:
    explicit Library(const char* name) {
#if defined(_WIN32)
        // Yalnız System32'de ara: uygulama ya da çalışma klasörüne konmuş aynı adlı bir DLL
        // yüklenmesin (DLL planting).
        m_handle = LoadLibraryExA(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
        m_handle = dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
    }
    explicit operator bool() const { return m_handle != nullptr; }

    template <typename F>
    F symbol(const char* name) const {
#if defined(_WIN32)
        return reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_handle), name)));
#else
        return reinterpret_cast<F>(dlsym(m_handle, name));
#endif
    }

private:
    void* m_handle = nullptr;
};

std::string architectureName(int major, int minor) {
    const int sm = major * 10 + minor;
    if (sm >= 100) return "Blackwell";
    if (sm >= 90) return "Hopper";
    if (sm >= 89) return "Ada";
    if (sm >= 80) return "Ampere";
    if (sm >= 75) return "Turing";
    if (sm >= 70) return "Volta";
    if (sm >= 60) return "Pascal";
    if (sm >= 50) return "Maxwell";
    return "Kepler ya da daha eski";
}

} // namespace

std::vector<DeviceInfo> probeCudaDevices() {
    std::vector<DeviceInfo> out;
#if defined(_WIN32)
    Library lib("nvcuda.dll");
#else
    Library lib("libcuda.so.1");
#endif
    if (!lib) return out;
    const auto cuInit = lib.symbol<FnInit>("cuInit");
    const auto cuDriverGetVersion = lib.symbol<FnDriverVersion>("cuDriverGetVersion");
    const auto cuDeviceGetCount = lib.symbol<FnGetCount>("cuDeviceGetCount");
    const auto cuDeviceGet = lib.symbol<FnGet>("cuDeviceGet");
    const auto cuDeviceGetName = lib.symbol<FnGetName>("cuDeviceGetName");
    const auto cuDeviceTotalMem = lib.symbol<FnTotalMem>("cuDeviceTotalMem_v2");
    const auto cuDeviceGetAttribute = lib.symbol<FnGetAttribute>("cuDeviceGetAttribute");
    if (!cuInit || !cuDeviceGetCount || !cuDeviceGet || !cuDeviceGetName || !cuDeviceGetAttribute) return out;
    if (cuInit(0) != 0) return out;
    int driver = 0;
    if (cuDriverGetVersion) cuDriverGetVersion(&driver);
    int count = 0;
    if (cuDeviceGetCount(&count) != 0) return out;
    for (int i = 0; i < count; ++i) {
        CUdevice dev = 0;
        if (cuDeviceGet(&dev, i) != 0) continue;
        DeviceInfo d;
        d.id = "cuda:" + std::to_string(i);
        d.kind = DeviceKind::Cuda;
        char name[256] = {};
        if (cuDeviceGetName(name, static_cast<int>(sizeof(name)) - 1, dev) == 0) d.name = name;
        size_t mem = 0;
        if (cuDeviceTotalMem && cuDeviceTotalMem(&mem, dev) == 0) d.memoryBytes = mem;
        cuDeviceGetAttribute(&d.computeMajor, kAttrComputeMajor, dev);
        cuDeviceGetAttribute(&d.computeMinor, kAttrComputeMinor, dev);
        d.hardwareRayTracing = d.computeMajor * 10 + d.computeMinor >= 75;
        d.usable = false;
        std::ostringstream note;
        note << architectureName(d.computeMajor, d.computeMinor) << ", sm_" << d.computeMajor << d.computeMinor;
        if (driver > 0) note << ", CUDA sürücüsü " << driver / 1000 << "." << (driver % 1000) / 10;
        note << (d.hardwareRayTracing ? "; RT çekirdekli (OptiX / Vulkan RT / DXR mümkün)"
                                      : "; RT çekirdeği yok (OptiX 9, DXR 1.1+ ve RTXPT çalışmaz; CUDA hesaplama mümkün)");
        note << ". GPU arka ucu bu sürümde yok (Faz 9).";
        d.note = note.str();
        out.push_back(std::move(d));
    }
    return out;
}

std::string cpuBrandName() {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    unsigned int regs[12] = {};
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int r[4];
    __cpuid(r, static_cast<int>(0x80000000u));
    if (static_cast<unsigned int>(r[0]) < 0x80000004u) return {};
    for (unsigned int i = 0; i < 3; ++i) {
        __cpuid(r, static_cast<int>(0x80000002u + i));
        std::memcpy(regs + i * 4, r, sizeof(r));
    }
#else
    if (__get_cpuid_max(0x80000000u, nullptr) < 0x80000004u) return {};
    for (unsigned int i = 0; i < 3; ++i)
        __get_cpuid(0x80000002u + i, &regs[i * 4], &regs[i * 4 + 1], &regs[i * 4 + 2], &regs[i * 4 + 3]);
#endif
    char brand[49] = {};
    std::memcpy(brand, regs, sizeof(regs));
    std::string s(brand);
    const size_t b = s.find_first_not_of(' ');
    const size_t e = s.find_last_not_of(' ');
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
#else
    return {};
#endif
}

} // namespace photon
