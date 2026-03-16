#pragma once

#include <atomic>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <memory>

namespace lockless {

// fixed-capacity lock-free stack using a pre-allocated array
// packs index + tag into 64 bits so we can CAS on a single uint64_t
// instead of needing 128-bit CAS (which MSVC doesn't do lock-free)
template<typename T>
class ArrayLockFreeStack {
    struct Node {
        T data;
        // atomic because another thread might read this while we're
        // popping and re-pushing the same node
        std::atomic<uint32_t> next; 
        
        Node() : next(UINT32_MAX) {}
    };

    static constexpr uint32_t NULL_INDEX = UINT32_MAX;

    // high 32 bits = tag (ABA counter), low 32 bits = index
    struct Head {
        uint32_t index;
        uint32_t tag;
    };

    static uint64_t pack(uint32_t index, uint32_t tag) noexcept {
        return (static_cast<uint64_t>(tag) << 32) | index;
    }

    static Head unpack(uint64_t val) noexcept {
        return { static_cast<uint32_t>(val), static_cast<uint32_t>(val >> 32) };
    }

    std::unique_ptr<Node[]> nodes_;
    size_t capacity_;
    
    std::atomic<uint64_t> head_;      // data stack
    std::atomic<uint64_t> free_head_; // free node pool

public:
    explicit ArrayLockFreeStack(size_t capacity) : capacity_(capacity) {
        nodes_ = std::make_unique<Node[]>(capacity);

        // chain free list: 0 -> 1 -> 2 ... -> NULL
        for (size_t i = 0; i < capacity - 1; ++i) {
            nodes_[i].next.store(static_cast<uint32_t>(i + 1), std::memory_order_relaxed);
        }
        nodes_[capacity - 1].next.store(NULL_INDEX, std::memory_order_relaxed);

        free_head_.store(pack(0, 0), std::memory_order_relaxed);
        head_.store(pack(NULL_INDEX, 0), std::memory_order_relaxed);
    }

    ~ArrayLockFreeStack() = default;

    ArrayLockFreeStack(const ArrayLockFreeStack&) = delete;
    ArrayLockFreeStack& operator=(const ArrayLockFreeStack&) = delete;

    bool try_push(const T& value) noexcept {
        uint32_t node_idx = pop_index(free_head_);
        if (node_idx == NULL_INDEX) {
            return false; // full
        }

        nodes_[node_idx].data = value;
        push_index(head_, node_idx);
        return true;
    }

    bool try_push(T&& value) noexcept {
        uint32_t node_idx = pop_index(free_head_);
        if (node_idx == NULL_INDEX) {
            return false;
        }

        nodes_[node_idx].data = std::move(value);
        push_index(head_, node_idx);
        return true;
    }

    bool try_pop(T& result) noexcept {
        uint32_t node_idx = pop_index(head_);
        if (node_idx == NULL_INDEX) {
            return false; // empty
        }

        result = std::move(nodes_[node_idx].data);
        push_index(free_head_, node_idx);
        return true;
    }

    bool is_lock_free() const noexcept {
        return head_.is_lock_free();
    }

    size_t capacity() const noexcept {
        return capacity_;
    }

private:
    void push_index(std::atomic<uint64_t>& target_head, uint32_t node_idx) noexcept {
        uint64_t old_head_raw = target_head.load(std::memory_order_relaxed);
        
        while (true) {
            Head old_head = unpack(old_head_raw);
            
            nodes_[node_idx].next.store(old_head.index, std::memory_order_relaxed);
            
            uint64_t new_head_raw = pack(node_idx, old_head.tag + 1);
            
            if (target_head.compare_exchange_weak(old_head_raw, new_head_raw,
                                                  std::memory_order_release,
                                                  std::memory_order_relaxed)) {
                return;
            }
        }
    }

    uint32_t pop_index(std::atomic<uint64_t>& target_head) noexcept {
        uint64_t old_head_raw = target_head.load(std::memory_order_acquire);
        
        while (true) {
            Head old_head = unpack(old_head_raw);
            
            if (old_head.index == NULL_INDEX) {
                return NULL_INDEX;
            }
            
            // read next — safe from data races because next is atomic,
            // and the tag protects us from ABA on the CAS below
            uint32_t next_idx = nodes_[old_head.index].next.load(std::memory_order_relaxed);
            
            uint64_t new_head_raw = pack(next_idx, old_head.tag + 1);
            
            if (target_head.compare_exchange_weak(old_head_raw, new_head_raw,
                                                  std::memory_order_acquire,
                                                  std::memory_order_acquire)) {
                return old_head.index;
            }
        }
    }
};

} // namespace lockless
