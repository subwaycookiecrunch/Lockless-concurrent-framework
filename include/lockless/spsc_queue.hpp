#pragma once

#include "common.hpp"
#include <atomic>
#include <cstddef>
#include <array>
#include <type_traits>

namespace lockless {

// wait-free single-producer single-consumer bounded queue
//
// much simpler than the MPMC ring buffer — no CAS loops at all.
// the producer only writes head, the consumer only writes tail,
// so there's zero contention. every operation completes in bounded
// steps, making this wait-free (not just lock-free).
//
// based on the classic Lamport queue with acquire/release fencing.
// use this when you have a dedicated producer-consumer pair and
// want maximum throughput with guaranteed latency.

template<typename T, size_t Size>
class SPSCQueue {
    static_assert((Size != 0) && ((Size & (Size - 1)) == 0), "Size must be a power of 2");
    static_assert(Size >= 2, "Size must be at least 2");

public:
    SPSCQueue() noexcept : head_(0), tail_(0) {}

    ~SPSCQueue() = default;

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // only call from the producer thread
    bool try_push(const T& item) noexcept {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next = (head + 1) & mask_;

        // if next == tail, queue is full
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;
        }

        buffer_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool try_push(T&& item) noexcept {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next = (head + 1) & mask_;

        if (next == tail_.load(std::memory_order_acquire)) {
            return false;
        }

        buffer_[head] = std::move(item);
        head_.store(next, std::memory_order_release);
        return true;
    }

    // only call from the consumer thread
    bool try_pop(T& item) noexcept {
        size_t tail = tail_.load(std::memory_order_relaxed);

        // if tail == head, queue is empty
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }

        item = std::move(buffer_[tail]);
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return true;
    }

    size_t capacity() const noexcept {
        // one slot is always empty (sentinel), so usable capacity is Size - 1
        return Size - 1;
    }

    bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

private:
    static constexpr size_t mask_ = Size - 1;

    // pad head and tail to separate cache lines
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_;
    std::array<T, Size> buffer_;
};

} // namespace lockless
