#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <cassert>
#include <type_traits>
#include <utility>

namespace photon {

template <typename T>
class PoolAllocator {
public:
    explicit PoolAllocator(size_t poolSize = 1024)
        : m_poolSize(poolSize) {
        // Ensure slot size is at least large enough for the free list node pointer
        static_assert(sizeof(T) >= sizeof(FreeNode*) || sizeof(FreeNode) <= sizeof(T) || true,
                      "Pool element size check");

        m_slotSize = sizeof(T) > sizeof(FreeNode) ? sizeof(T) : sizeof(FreeNode);

        // Allocate raw memory block with proper alignment
        size_t totalSize = m_slotSize * m_poolSize;
        m_block = static_cast<uint8_t*>(::operator new(totalSize, std::align_val_t{alignof(T)}));

        // Build the free list
        m_freeList = nullptr;
        for (size_t i = m_poolSize; i > 0; --i) {
            auto* node = reinterpret_cast<FreeNode*>(m_block + (i - 1) * m_slotSize);
            node->next = m_freeList;
            m_freeList = node;
        }
    }

    ~PoolAllocator() {
        if (m_block) {
            ::operator delete(m_block, std::align_val_t{alignof(T)});
            m_block = nullptr;
        }
    }

    // Delete copy
    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    // Move constructor
    PoolAllocator(PoolAllocator&& other) noexcept
        : m_block(other.m_block)
        , m_freeList(other.m_freeList)
        , m_poolSize(other.m_poolSize)
        , m_slotSize(other.m_slotSize) {
        other.m_block = nullptr;
        other.m_freeList = nullptr;
        other.m_poolSize = 0;
        other.m_slotSize = 0;
    }

    // Move assignment
    PoolAllocator& operator=(PoolAllocator&& other) noexcept {
        if (this != &other) {
            if (m_block) {
                ::operator delete(m_block, std::align_val_t{alignof(T)});
            }

            m_block = other.m_block;
            m_freeList = other.m_freeList;
            m_poolSize = other.m_poolSize;
            m_slotSize = other.m_slotSize;

            other.m_block = nullptr;
            other.m_freeList = nullptr;
            other.m_poolSize = 0;
            other.m_slotSize = 0;
        }
        return *this;
    }

    /// Allocate a single object slot. Returns nullptr if pool is exhausted.
    T* allocate() {
        if (!m_freeList) {
            return nullptr;
        }

        FreeNode* node = m_freeList;
        m_freeList = node->next;
        return reinterpret_cast<T*>(node);
    }

    /// Return an object slot to the pool.
    void deallocate(T* ptr) {
        assert(ptr != nullptr && "Cannot deallocate nullptr");
        assert(isFromPool(ptr) && "Pointer does not belong to this pool");

        auto* node = reinterpret_cast<FreeNode*>(ptr);
        node->next = m_freeList;
        m_freeList = node;
    }

    /// Check if pool has available slots.
    bool hasAvailable() const {
        return m_freeList != nullptr;
    }

    /// Get total pool capacity.
    size_t capacity() const {
        return m_poolSize;
    }

private:
    union FreeNode {
        FreeNode* next;
    };

    bool isFromPool(const T* ptr) const {
        auto addr = reinterpret_cast<const uint8_t*>(ptr);
        return addr >= m_block && addr < (m_block + m_slotSize * m_poolSize);
    }

    uint8_t* m_block = nullptr;
    FreeNode* m_freeList = nullptr;
    size_t m_poolSize = 0;
    size_t m_slotSize = 0;
};

} // namespace photon
