#pragma once

#include "thread_pool.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <future>
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

    // Run sequentially if range is small enough
    if (range <= grainSize) {
        for (int64_t i = begin; i < end; ++i) {
            func(i);
        }
        return;
    }

    auto& pool = getThreadPool();
    std::vector<std::future<void>> futures;

    for (int64_t i = begin; i < end; i += grainSize) {
        int64_t chunkEnd = std::min(i + grainSize, end);
        futures.push_back(pool.submit([&func, i, chunkEnd]() {
            for (int64_t j = i; j < chunkEnd; ++j) {
                func(j);
            }
        }));
    }

    for (auto& future : futures) {
        future.get();
    }
}

inline void parallelFor2D(int width, int height, const std::function<void(int, int, int, int)>& func, int tileSize = 16) {
    if (width <= 0 || height <= 0) {
        return;
    }

    if (tileSize <= 0) {
        tileSize = 16;
    }

    auto& pool = getThreadPool();
    std::vector<std::future<void>> futures;

    for (int y = 0; y < height; y += tileSize) {
        for (int x = 0; x < width; x += tileSize) {
            int xBegin = x;
            int xEnd = std::min(x + tileSize, width);
            int yBegin = y;
            int yEnd = std::min(y + tileSize, height);

            futures.push_back(pool.submit([&func, xBegin, xEnd, yBegin, yEnd]() {
                func(xBegin, xEnd, yBegin, yEnd);
            }));
        }
    }

    for (auto& future : futures) {
        future.get();
    }
}

} // namespace photon
