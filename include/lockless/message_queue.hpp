#pragma once

#include "ring_buffer.hpp"
#include <vector>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace lockless {

struct MessageHandle {
    size_t offset;
    size_t size;
};

class SharedMemoryArena {
    std::vector<uint8_t> buffer_;
    std::atomic<size_t> write_offset_;
    size_t size_;

public:
    explicit SharedMemoryArena(size_t size) : buffer_(size), write_offset_(0), size_(size) {}

    // bump allocator — throws when full, callers should size the arena
    // generously or handle recycling themselves
    void* allocate(size_t bytes) {
        size_t current = write_offset_.load(std::memory_order_relaxed);

        while (true) {
            if (current + bytes > size_) {
                throw std::runtime_error("Arena full");
            }

            // CAS loop so two threads can't both "succeed" past the bounds check
            if (write_offset_.compare_exchange_weak(current, current + bytes,
                    std::memory_order_relaxed, std::memory_order_relaxed)) {
                return &buffer_[current];
            }
            // current gets updated by CAS failure, retry
        }
    }
    
    void* get(size_t offset) noexcept {
        if (offset >= size_) return nullptr;
        return &buffer_[offset];
    }
    
    size_t get_offset(void* ptr) noexcept {
        uint8_t* p = static_cast<uint8_t*>(ptr);
        if (p < buffer_.data() || p >= buffer_.data() + size_) return SIZE_MAX;
        return p - buffer_.data();
    }
};

template<size_t QueueSize = 1024, size_t ArenaSize = 1024 * 1024>
class ZeroCopyQueue {
    RingBuffer<MessageHandle, QueueSize> queue_;
    SharedMemoryArena arena_;

public:
    ZeroCopyQueue() : arena_(ArenaSize) {}

    void* prepare_message(size_t size) {
        return arena_.allocate(size);
    }

    bool push_message(void* ptr, size_t size) noexcept {
        size_t offset = arena_.get_offset(ptr);
        if (offset == SIZE_MAX) return false;
        
        return queue_.try_push({offset, size});
    }

    bool pop_message(MessageHandle& handle) noexcept {
        return queue_.try_pop(handle);
    }

    void* get_message_data(const MessageHandle& handle) noexcept {
        return arena_.get(handle.offset);
    }
};

} // namespace lockless
