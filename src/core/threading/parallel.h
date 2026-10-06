// parallel.h — Görüntüyü karolara bölüp thread havuzunda paralel işleyen yardımcılar.
//
// parallelFor2D, W×H görüntüyü tileSize×tileSize karolara böler ve her karoyu
// havuza bir iş olarak verir. İsteğe bağlı `cancel` bayrağı set edilince henüz
// başlamamış karolar hemen döner; böylece kamera hareket ettiğinde eski pass
// bir karo süresi içinde (milisaniyeler) durur.
#pragma once

#include "thread_pool.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <vector>

namespace photon {

/// Uygulama ömrü boyunca yaşayan ortak havuz. Bir çekirdek UI thread'ine kalsın
/// diye işçi sayısı hardware_concurrency - 1 (en az 1).
inline ThreadPool& getThreadPool() {
    static ThreadPool pool(std::max(1u, std::thread::hardware_concurrency() > 1
                                           ? std::thread::hardware_concurrency() - 1
                                           : 1u));
    return pool;
}

/// Karoları paralel işler. Bir karo istisna atarsa diğer karoların bitmesi
/// beklenir, sonra ilk istisna yeniden fırlatılır: işçiler çağıranın yığınındaki
/// (stack) verilere dokunurken fonksiyondan erken çıkmak çökmeye yol açardı.
/// @return Tüm karolar işlendiyse true; iptal edildiyse false.
inline bool parallelFor2D(int width, int height, const std::function<void(int, int, int, int)>& func,
                          int tileSize = 16, ThreadPool* poolOverride = nullptr,
                          const std::atomic<bool>* cancel = nullptr) {
    if (width <= 0 || height <= 0) {
        return true;
    }

    if (tileSize <= 0) {
        tileSize = 16;
    }

    ThreadPool& pool = poolOverride ? *poolOverride : getThreadPool();
    std::vector<std::future<void>> futures;
    futures.reserve(static_cast<size_t>((width + tileSize - 1) / tileSize) *
                    static_cast<size_t>((height + tileSize - 1) / tileSize));

    // Referansla yakalamak güvenli: aşağıda her future beklenmeden dönülmüyor.
    for (int y = 0; y < height; y += tileSize) {
        for (int x = 0; x < width; x += tileSize) {
            int xBegin = x;
            int xEnd = std::min(x + tileSize, width);
            int yBegin = y;
            int yEnd = std::min(y + tileSize, height);

            futures.push_back(pool.submit([&func, cancel, xBegin, xEnd, yBegin, yEnd]() {
                if (cancel && cancel->load(std::memory_order_relaxed)) return;
                func(xBegin, xEnd, yBegin, yEnd);
            }));
        }
    }

    std::exception_ptr firstError;
    for (auto& future : futures) {
        try {
            future.get();
        } catch (...) {
            if (!firstError) firstError = std::current_exception();
        }
    }
    if (firstError) std::rethrow_exception(firstError);
    return !(cancel && cancel->load(std::memory_order_relaxed));
}

} // namespace photon
