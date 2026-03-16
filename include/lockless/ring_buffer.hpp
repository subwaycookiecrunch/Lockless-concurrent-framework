#pragma once

#include "common.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <array>
#include <type_traits>
#include <new>

namespace lockless {

template<typename T, size_t Size>
class RingBuffer {
    static_assert((Size != 0) && ((Size & (Size - 1)) == 0), "Size must be a power of 2");

public:
    RingBuffer() noexcept {
        for (size_t i = 0; i < Size; ++i) {
            sequences_[i].store(i, std::memory_order_relaxed);
        }
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    ~RingBuffer() = default;

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    bool try_push(const T& item) noexcept {
        size_t head = head_.load(std::memory_order_relaxed);

        while (true) {
            size_t index = head & mask_;
            size_t seq = sequences_[index].load(std::memory_order_acquire);
            intptr_t diff = (intptr_t)seq - (intptr_t)head;

            if (diff == 0) {
                if (head_.compare_exchange_weak(head, head + 1, std::memory_order_relaxed)) {
                    buffer_[index] = item;
                    sequences_[index].store(head + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;
            } else {
                head = head_.load(std::memory_order_relaxed);
            }
        }
    }

    bool try_push(T&& item) noexcept {
        size_t head = head_.load(std::memory_order_relaxed);

        while (true) {
            size_t index = head & mask_;
            size_t seq = sequences_[index].load(std::memory_order_acquire);
            intptr_t diff = (intptr_t)seq - (intptr_t)head;

            if (diff == 0) {
                if (head_.compare_exchange_weak(head, head + 1, std::memory_order_relaxed)) {
                    buffer_[index] = std::move(item);
                    sequences_[index].store(head + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;
            } else {
                head = head_.load(std::memory_order_relaxed);
            }
        }
    }

    bool try_pop(T& item) noexcept {
        size_t tail = tail_.load(std::memory_order_relaxed);

        while (true) {
            size_t index = tail & mask_;
            size_t seq = sequences_[index].load(std::memory_order_acquire);
            intptr_t diff = (intptr_t)seq - (intptr_t)(tail + 1);

            if (diff == 0) {
                if (tail_.compare_exchange_weak(tail, tail + 1, std::memory_order_relaxed)) {
                    item = std::move(buffer_[index]);
                    sequences_[index].store(tail + mask_ + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;
            } else {
                tail = tail_.load(std::memory_order_relaxed);
            }
        }
    }

    // iterative batch — true MPMC batch reservation is tricky without
    // breaking the sequence invariant, so just push one by one
    size_t try_push_batch(const T* items, size_t count) noexcept {
        size_t pushed = 0;
        for (size_t i = 0; i < count; ++i) {
            if (!try_push(items[i])) break;
            ++pushed;
        }
        return pushed;
    }

    size_t try_pop_batch(T* items, size_t max_count) noexcept {
        size_t popped = 0;
        for (size_t i = 0; i < max_count; ++i) {
            if (!try_pop(items[i])) break;
            ++popped;
        }
        return popped;
    }

private:
    static constexpr size_t mask_ = Size - 1;

    std::array<std::atomic<size_t>, Size> sequences_;
    std::array<T, Size> buffer_;

    // pad these onto separate cache lines to avoid false sharing
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_;
};

} // namespace lockless
