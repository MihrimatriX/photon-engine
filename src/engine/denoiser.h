// denoiser.h — Intel Open Image Denoise (OIDN) ile gürültü giderme.
//
// Path tracing az örnekle gürültülü görüntü üretir. OIDN, yapay sinir ağıyla bu
// gürültüyü temizler; albedo ve normal kanalları (AOV) verilirse dokuları ve
// kenarları çok daha iyi korur. OIDN derlemede yoksa yalnız 3×3 yumuşatma
// uygulanır (gerçek bir gürültü giderici değildir; arayüz bunu açıkça söyler).
#pragma once

#include "core/image/image.h"
#include <memory>
#include <mutex>

namespace photon {

/// OIDN cihazını ve filtresini bir kez kurup tekrar kullanan gürültü giderici.
/// Bir örnek aynı anda tek thread'den kullanılmalı (iç kilit bunu zorlar).
class Denoiser {
public:
    enum class Quality { Fast, Balanced, High };

    Denoiser();
    ~Denoiser();
    Denoiser(const Denoiser&) = delete;
    Denoiser& operator=(const Denoiser&) = delete;

    /// Ortalaması alınmış HDR görüntüyü yerinde temizler. AOV'ler isteğe bağlı,
    /// aynı çözünürlükte olmalı. Hata olursa yumuşatmaya düşer ve false döner.
    bool run(Image& color, const Image* albedo, const Image* normal, Quality quality = Quality::High);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::mutex m_mutex;
};

/// Paylaşılan bir Denoiser ile yerinde gürültü giderme (Quality::High).
bool denoiseImage(Image& color, const Image* albedo = nullptr, const Image* normal = nullptr);

/// Kopya üzerinde gürültü giderme; kaynak değişmez.
Image denoiseCopy(const Image& color);

/// Bu derleme OIDN ile bağlandıysa true.
bool denoiseAvailable();

} // namespace photon
