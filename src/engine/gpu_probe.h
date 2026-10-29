// gpu_probe.h — Makinedeki NVIDIA GPU'ları CUDA sürücü API'siyle sorgular.
//
// Sürücü kitaplığı (Windows: nvcuda.dll, Linux: libcuda.so.1) çalışma zamanında yüklenir;
// derleme CUDA Toolkit'e bağlı değildir ve sürücüsü olmayan makinede boş liste döner.
// Yalnız bilgi toplanır (ad, bellek, hesaplama yeteneği); hiçbir çekirdek çalıştırılmaz.
#pragma once

#include "engine/render_device.h"

#include <vector>

namespace photon {

/// Bulunan her GPU için bir DeviceInfo (kind = Cuda, usable = false: arka uç henüz yok).
std::vector<DeviceInfo> probeCudaDevices();

/// İşlemcinin marka adı (x86'da CPUID), bulunamazsa boş.
std::string cpuBrandName();

} // namespace photon
