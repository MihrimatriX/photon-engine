#pragma once

#include "thread_pool.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <vector>

namespace photon {

inline ThreadPool& getThreadPool() {
    static ThreadPool pool;
    return pool;
}

inline void parallelFor(int64_t begin, int64_t end, const std::function<void(int64_t)>& func, int64_t grainSize = 1) {
    if (grainSize <= 0) {
        grainSize = 1;
    }

    int64_t range = end - begin;
    if (range <= 0) {
        return;
    }

    if (range <= grainSize) {
        for (int64_t i = begin; i < end; ++i) {
            func(i);
        }
        return;
    }

    // Hold a shared copy so worker lifetimes can't outlive a temporary std::function.
    auto sharedFunc = std::make_shared<std::function<void(int64_t)>>(func);
    auto& pool = getThreadPool();
    std::vector<std::future<void>> futures;

    for (int64_t i = begin; i < end; i += grainSize) {
        int64_t chunkEnd = std::min(i + grainSize, end);
        futures.push_back(pool.submit([sharedFunc, i, chunkEnd]() {
            for (int64_t j = i; j < chunkEnd; ++j) {
                (*sharedFunc)(j);
            }
        }));
    }

    for (auto& future : futures) {
        future.get();
    }
}

inline void parallelFor2D(int width, int height, const std::function<void(int, int, int, int)>& func,
                          int tileSize = 16, ThreadPool* poolOverride = nullptr) {
    if (width <= 0 || height <= 0) {
        return;
    }

    if (tileSize <= 0) {
        tileSize = 16;
    }

    // Copy into shared_ptr: workers must not keep a reference to a stack temporary.
    auto sharedFunc = std::make_shared<std::function<void(int, int, int, int)>>(func);

    ThreadPool& pool = poolOverride ? *poolOverride : getThreadPool();
    std::vector<std::future<void>> futures;
    futures.reserve(static_cast<size_t>((width + tileSize - 1) / tileSize) *
                    static_cast<size_t>((height + tileSize - 1) / tileSize));

    for (int y = 0; y < height; y += tileSize) {
        for (int x = 0; x < width; x += tileSize) {
            int xBegin = x;
            int xEnd = std::min(x + tileSize, width);
            int yBegin = y;
            int yEnd = std::min(y + tileSize, height);

            futures.push_back(pool.submit([sharedFunc, xBegin, xEnd, yBegin, yEnd]() {
                (*sharedFunc)(xBegin, xEnd, yBegin, yEnd);
            }));
        }
    }

    for (auto& future : futures) {
        future.get();
    }
}

} // namespace photon
