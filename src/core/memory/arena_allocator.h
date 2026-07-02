#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace photon {

class ArenaAllocator {
public:
    explicit ArenaAllocator(size_t blockSize = 1024 * 1024); // 1MB default
    ~ArenaAllocator();

    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;
    ArenaAllocator(ArenaAllocator&&) noexcept;
    ArenaAllocator& operator=(ArenaAllocator&&) noexcept;

    void* allocate(size_t bytes, size_t alignment = 16);
    void reset();
    void release();
    size_t bytesAllocated() const;

private:
    void allocateBlock(size_t minSize);

    size_t m_blockSize;
    std::vector<uint8_t*> m_blocks;
    std::vector<size_t> m_blockSizes;
    uint8_t* m_currentBlock = nullptr;
    size_t m_currentBlockSize = 0;
    size_t m_currentOffset = 0;
    size_t m_totalAllocated = 0;
};

} // namespace photon
