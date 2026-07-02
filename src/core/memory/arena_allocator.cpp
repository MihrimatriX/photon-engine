#include "arena_allocator.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace photon {

ArenaAllocator::ArenaAllocator(size_t blockSize)
    : m_blockSize(blockSize) {}

ArenaAllocator::~ArenaAllocator() {
    release();
}

ArenaAllocator::ArenaAllocator(ArenaAllocator&& other) noexcept
    : m_blockSize(other.m_blockSize)
    , m_blocks(std::move(other.m_blocks))
    , m_blockSizes(std::move(other.m_blockSizes))
    , m_currentBlock(other.m_currentBlock)
    , m_currentBlockSize(other.m_currentBlockSize)
    , m_currentOffset(other.m_currentOffset)
    , m_totalAllocated(other.m_totalAllocated) {
    other.m_currentBlock = nullptr;
    other.m_currentBlockSize = 0;
    other.m_currentOffset = 0;
    other.m_totalAllocated = 0;
}

ArenaAllocator& ArenaAllocator::operator=(ArenaAllocator&& other) noexcept {
    if (this != &other) {
        release();

        m_blockSize = other.m_blockSize;
        m_blocks = std::move(other.m_blocks);
        m_blockSizes = std::move(other.m_blockSizes);
        m_currentBlock = other.m_currentBlock;
        m_currentBlockSize = other.m_currentBlockSize;
        m_currentOffset = other.m_currentOffset;
        m_totalAllocated = other.m_totalAllocated;

        other.m_currentBlock = nullptr;
        other.m_currentBlockSize = 0;
        other.m_currentOffset = 0;
        other.m_totalAllocated = 0;
    }
    return *this;
}

void ArenaAllocator::allocateBlock(size_t minSize) {
    size_t size = std::max(m_blockSize, minSize);
    auto* block = new uint8_t[size];
    m_blocks.push_back(block);
    m_blockSizes.push_back(size);
    m_currentBlock = block;
    m_currentBlockSize = size;
    m_currentOffset = 0;
}

void* ArenaAllocator::allocate(size_t bytes, size_t alignment) {
    if (bytes == 0) {
        return nullptr;
    }

    // Calculate aligned offset within current block
    if (m_currentBlock) {
        uintptr_t currentAddr = reinterpret_cast<uintptr_t>(m_currentBlock) + m_currentOffset;
        uintptr_t alignedAddr = (currentAddr + alignment - 1) & ~(alignment - 1);
        size_t alignedOffset = static_cast<size_t>(alignedAddr - reinterpret_cast<uintptr_t>(m_currentBlock));

        if (alignedOffset + bytes <= m_currentBlockSize) {
            m_currentOffset = alignedOffset + bytes;
            m_totalAllocated += bytes;
            return reinterpret_cast<void*>(alignedAddr);
        }
    }

    // Current block doesn't have enough space, allocate a new one
    allocateBlock(std::max(m_blockSize, bytes + alignment));

    // Align within the fresh block
    uintptr_t currentAddr = reinterpret_cast<uintptr_t>(m_currentBlock) + m_currentOffset;
    uintptr_t alignedAddr = (currentAddr + alignment - 1) & ~(alignment - 1);
    size_t alignedOffset = static_cast<size_t>(alignedAddr - reinterpret_cast<uintptr_t>(m_currentBlock));

    m_currentOffset = alignedOffset + bytes;
    m_totalAllocated += bytes;
    return reinterpret_cast<void*>(alignedAddr);
}

void ArenaAllocator::reset() {
    m_currentOffset = 0;
    m_totalAllocated = 0;

    if (!m_blocks.empty()) {
        m_currentBlock = m_blocks[0];
        m_currentBlockSize = m_blockSizes[0];
    }
}

void ArenaAllocator::release() {
    for (auto* block : m_blocks) {
        delete[] block;
    }
    m_blocks.clear();
    m_blockSizes.clear();
    m_currentBlock = nullptr;
    m_currentBlockSize = 0;
    m_currentOffset = 0;
    m_totalAllocated = 0;
}

size_t ArenaAllocator::bytesAllocated() const {
    return m_totalAllocated;
}

} // namespace photon
