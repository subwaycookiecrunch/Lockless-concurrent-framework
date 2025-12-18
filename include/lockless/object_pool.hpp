#pragma once

#include "array_stack.hpp"
#include <functional>
#include <optional>

namespace lockless {

/**
 * @brief A lock-free object pool backed by ArrayLockFreeStack.
 * 
 * Maintains a pre-allocated array of T objects and a lock-free stack of
 * free indices. acquire() pops an index, release() pushes it back.
 */
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

    /**
     * @brief Acquires an object from the pool.
     * @return Pointer to the object, or nullptr if pool is empty.
     */
    T* acquire() {
        uint32_t idx;
        if (free_indices_.try_pop(idx)) {
            return &pool_storage_[idx];
        }
        return nullptr;
    }

    /**
     * @brief Releases an object back to the pool.
     * @param ptr Must have been obtained from this pool.
     */
    void release(T* ptr) {
        if (!ptr) return;
        
        ptrdiff_t offset = ptr - pool_storage_.get();
        
        if (offset < 0 || static_cast<size_t>(offset) >= capacity_) {
            return;
        }
        
        uint32_t idx = static_cast<uint32_t>(offset);
        free_indices_.try_push(idx);
    }
    
    uint32_t get_index(T* ptr) const {
        return static_cast<uint32_t>(ptr - pool_storage_.get());
    }
    
    T* get_by_index(uint32_t idx) {
        if (idx >= capacity_) return nullptr;
        return &pool_storage_[idx];
    }
    
    size_t capacity() const { return capacity_; }
};

} // namespace lockless
