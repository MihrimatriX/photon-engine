// aligned.h — Platformdan bağımsız hizalı bellek ayırma/serbest bırakma.
// SIMD yüklemeleri ve önbellek satırı (64 bayt) hizası için kullanılır; Windows'ta
// _aligned_malloc/_aligned_free, diğer sistemlerde std::aligned_alloc/free çiftidir.
#pragma once

#include <cstddef>
#include <cstdlib>

#if defined(_MSC_VER) || defined(_WIN32)
#include <malloc.h>
#endif

namespace photon {

/// Allocate memory with specified alignment.
/// Returns nullptr on failure.
inline void* alignedAlloc(size_t size, size_t alignment) {
    if (size == 0) {
        return nullptr;
    }

#if defined(_MSC_VER) || defined(_WIN32)
    return _aligned_malloc(size, alignment);
#else
    // std::aligned_alloc requires size to be a multiple of alignment
    // (x + a − 1) & ~(a − 1): a ikinin kuvveti olduğunda x'i a'nın katına yukarı yuvarlar.
    size_t alignedSize = (size + alignment - 1) & ~(alignment - 1);
    return std::aligned_alloc(alignment, alignedSize);
#endif
}

/// Free memory allocated by alignedAlloc.
inline void alignedFree(void* ptr) {
    if (!ptr) {
        return;
    }

#if defined(_MSC_VER) || defined(_WIN32)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

} // namespace photon
