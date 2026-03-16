#pragma once

#include "array_stack.hpp"
#include <functional>
#include <optional>

namespace lockless {

// lock-free object pool backed by ArrayLockFreeStack
// pre-allocates N objects, acquire() pops a free index, release() pushes it back
template<typename T>
class LockFreeObjectPool {
    std::unique_ptr<T[]> pool_storage_;
    size_t capacity_;
    
    ArrayLockFreeStack<uint32_t> free_indices_;

public:
    explicit LockFreeObjectPool(size_t capacity) 
        : capacity_(capacity), free_indices_(capacity) {
        
        pool_storage_ = std::make_unique<T[]>(capacity);
        
        for (uint32_t i = 0; i < capacity; ++i) {
            if (!free_indices_.try_push(i)) {
                throw std::runtime_error("Failed to initialize object pool");
            }
        }
    }

    LockFreeObjectPool(const LockFreeObjectPool&) = delete;
    LockFreeObjectPool& operator=(const LockFreeObjectPool&) = delete;

    T* acquire() noexcept {
        uint32_t idx;
        if (free_indices_.try_pop(idx)) {
            return &pool_storage_[idx];
        }
        return nullptr;
    }

    void release(T* ptr) noexcept {
        if (!ptr) return;
        
        ptrdiff_t offset = ptr - pool_storage_.get();
        
        if (offset < 0 || static_cast<size_t>(offset) >= capacity_) {
            return;
        }
        
        uint32_t idx = static_cast<uint32_t>(offset);
        free_indices_.try_push(idx);
    }
    
    uint32_t get_index(T* ptr) const noexcept {
        return static_cast<uint32_t>(ptr - pool_storage_.get());
    }
    
    T* get_by_index(uint32_t idx) noexcept {
        if (idx >= capacity_) return nullptr;
        return &pool_storage_[idx];
    }
    
    size_t capacity() const noexcept { return capacity_; }
};

} // namespace lockless
