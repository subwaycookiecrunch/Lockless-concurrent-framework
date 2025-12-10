#pragma once

#include <atomic>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <memory>

namespace lockless {

/**
 * @brief A fixed-capacity, zero-allocation, lock-free stack.
 * 
 * This implementation uses a pre-allocated array of nodes and 32-bit indices
 * instead of pointers. This eliminates 'new/delete' on the hot path and
 * avoids the 16-byte CAS problem on MSVC by packing the Head (Index + Tag)
 * into a single 64-bit atomic.
 * 
 * @tparam T The type of data to store.
 */
template<typename T>
class ArrayLockFreeStack {
    struct Node {
        T data;
        // Atomic next index to prevent data races when a popping thread reads 'next'
        // while another thread pops and re-pushes the same node concurrently.
        std::atomic<uint32_t> next; 
        
        Node() : next(UINT32_MAX) {}
    };

    static constexpr uint32_t NULL_INDEX = UINT32_MAX;

    // 64-bit head: 32-bit tag (high), 32-bit index (low)
    struct Head {
        uint32_t index;
        uint32_t tag;
    };

    // Helper to pack/unpack Head
    static uint64_t pack(uint32_t index, uint32_t tag) {
        return (static_cast<uint64_t>(tag) << 32) | index;
    }

    static Head unpack(uint64_t val) {
        return { static_cast<uint32_t>(val), static_cast<uint32_t>(val >> 32) };
    }

    // Storage
    std::unique_ptr<Node[]> nodes_;
    size_t capacity_;
    
    // Heads for the two stacks
    std::atomic<uint64_t> head_;      // Stack of filled nodes (LIFO)
    std::atomic<uint64_t> free_head_; // Stack of free nodes

public:
    explicit ArrayLockFreeStack(size_t capacity) : capacity_(capacity) {
        nodes_ = std::make_unique<Node[]>(capacity);

        // Initialize free list: 0 -> 1 -> 2 ... -> NULL
        for (size_t i = 0; i < capacity - 1; ++i) {
            nodes_[i].next.store(static_cast<uint32_t>(i + 1), std::memory_order_relaxed);
        }
        nodes_[capacity - 1].next.store(NULL_INDEX, std::memory_order_relaxed);

        // Free head points to 0, tag 0
        free_head_.store(pack(0, 0), std::memory_order_relaxed);

        // Data head points to NULL, tag 0
        head_.store(pack(NULL_INDEX, 0), std::memory_order_relaxed);
    }

    ~ArrayLockFreeStack() = default;

    // Non-copyable
    ArrayLockFreeStack(const ArrayLockFreeStack&) = delete;
    ArrayLockFreeStack& operator=(const ArrayLockFreeStack&) = delete;

    /**
     * @brief Pushes an item onto the stack.
     * @return true if successful, false if stack is full.
     */
    bool try_push(const T& value) {
        // 1. Pop a free node index from the free list
        uint32_t node_idx = pop_index(free_head_);
        if (node_idx == NULL_INDEX) {
            return false; // Stack is full (no free nodes)
        }

        // 2. Set data
        // We own this node exclusively now, so relaxed store is fine for data
        // IF we ensure release ordering when publishing the node index.
        nodes_[node_idx].data = value;

        // 3. Push the node index to the data stack
        push_index(head_, node_idx);
        return true;
    }

    /**
     * @brief Pops an item from the stack.
     * @return true if successful, false if stack is empty.
     */
    bool try_pop(T& result) {
        // 1. Pop a node index from the data stack
        uint32_t node_idx = pop_index(head_);
        if (node_idx == NULL_INDEX) {
            return false; // Stack is empty
        }

        // 2. Read data
        result = nodes_[node_idx].data;

        // 3. Return the node index to the free list
        push_index(free_head_, node_idx);
        return true;
    }

    bool is_lock_free() const {
        return head_.is_lock_free();
    }

    size_t capacity() const {
        return capacity_;
    }

private:
    // Generic helper to push an index onto a stack (represented by atomic head)
    void push_index(std::atomic<uint64_t>& target_head, uint32_t node_idx) {
        uint64_t old_head_raw = target_head.load(std::memory_order_relaxed);
        
        while (true) {
            Head old_head = unpack(old_head_raw);
            
            // Point our node to the current head
            nodes_[node_idx].next.store(old_head.index, std::memory_order_relaxed);
            
            // Try to swap head to our node, incrementing tag
            uint64_t new_head_raw = pack(node_idx, old_head.tag + 1);
            
            if (target_head.compare_exchange_weak(old_head_raw, new_head_raw,
                                                  std::memory_order_release,
                                                  std::memory_order_relaxed)) {
                return;
            }
            // CAS failed, old_head_raw is updated, retry
        }
    }

    // Generic helper to pop an index from a stack
    uint32_t pop_index(std::atomic<uint64_t>& target_head) {
        uint64_t old_head_raw = target_head.load(std::memory_order_acquire);
        
        while (true) {
            Head old_head = unpack(old_head_raw);
            
            if (old_head.index == NULL_INDEX) {
                return NULL_INDEX; // Empty
            }
            
            // Read the next pointer.
            // This is the critical read. If another thread pops this node
            // and re-pushes it elsewhere, 'next' might change.
            // But the 'tag' in 'old_head' protects the CAS.
            // We just need to ensure this read doesn't race with a write.
            // Since 'next' is atomic, it is safe from data races.
            uint32_t next_idx = nodes_[old_head.index].next.load(std::memory_order_relaxed);
            
            // Try to swap head to next, incrementing tag
            uint64_t new_head_raw = pack(next_idx, old_head.tag + 1);
            
            if (target_head.compare_exchange_weak(old_head_raw, new_head_raw,
                                                  std::memory_order_acquire,
                                                  std::memory_order_acquire)) {
                return old_head.index;
            }
            // CAS failed, old_head_raw is updated, retry
        }
    }
};

} // namespace lockless
