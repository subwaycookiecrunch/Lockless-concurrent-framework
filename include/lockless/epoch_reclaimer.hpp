#pragma once

#include <atomic>
#include <vector>
#include <functional>
#include <thread>
#include <cstdint>
#include <array>
#include <mutex>

namespace lockless {

// Epoch-based memory reclamation
//
// The idea: instead of immediately freeing memory that other threads might
// still be reading, we defer the free until all threads have "moved past"
// the point where they could have had a reference to it.
//
// There are 3 epochs (0, 1, 2). When a thread enters a critical section
// it records the current global epoch. Objects are retired into retire lists
// tagged by epoch. When all active threads have advanced past an epoch,
// everything in that epoch's retire list can be safely freed.
//
// Based on the scheme described in:
//   K. Fraser, "Practical lock-freedom", PhD thesis, Cambridge, 2004

class EpochReclaimer {
    static constexpr size_t MAX_THREADS = 64;
    static constexpr size_t NUM_EPOCHS = 3;

    struct RetiredNode {
        void* ptr;
        std::function<void(void*)> deleter;
    };

    struct alignas(64) ThreadSlot {
        std::atomic<uint64_t> local_epoch{UINT64_MAX}; // UINT64_MAX = not active
        bool in_use{false};
    };

    std::atomic<uint64_t> global_epoch_{0};
    std::array<ThreadSlot, MAX_THREADS> slots_{};

    // per-epoch retire lists, each protected by its own mutex
    struct EpochList {
        std::vector<RetiredNode> nodes;
        std::mutex mtx;
    };
    std::array<EpochList, NUM_EPOCHS> retire_lists_;

public:
    EpochReclaimer() = default;

    ~EpochReclaimer() {
        for (auto& el : retire_lists_) {
            for (auto& node : el.nodes) {
                node.deleter(node.ptr);
            }
        }
    }

    EpochReclaimer(const EpochReclaimer&) = delete;
    EpochReclaimer& operator=(const EpochReclaimer&) = delete;

    // each thread needs an ID, call this once at thread start
    // returns -1 if no slots left
    int register_thread() noexcept {
        for (size_t i = 0; i < MAX_THREADS; ++i) {
            if (!slots_[i].in_use) {
                slots_[i].in_use = true;
                slots_[i].local_epoch.store(UINT64_MAX, std::memory_order_relaxed);
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void unregister_thread(int tid) noexcept {
        if (tid < 0 || tid >= static_cast<int>(MAX_THREADS)) return;
        slots_[tid].local_epoch.store(UINT64_MAX, std::memory_order_release);
        slots_[tid].in_use = false;
    }

    // call this when entering a read-side critical section
    void enter_critical(int tid) noexcept {
        uint64_t epoch = global_epoch_.load(std::memory_order_relaxed);
        slots_[tid].local_epoch.store(epoch, std::memory_order_release);
        // fence so that any loads in the critical section happen after
        // we've published our epoch
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    void leave_critical(int tid) noexcept {
        slots_[tid].local_epoch.store(UINT64_MAX, std::memory_order_release);
    }

    // schedule ptr for deletion once it's safe
    template<typename T>
    void retire(T* ptr) {
        uint64_t epoch = global_epoch_.load(std::memory_order_relaxed);
        size_t idx = epoch % NUM_EPOCHS;

        {
            std::lock_guard<std::mutex> lock(retire_lists_[idx].mtx);
            retire_lists_[idx].nodes.push_back({
                static_cast<void*>(ptr),
                [](void* p) { delete static_cast<T*>(p); }
            });
        }

        try_advance();
    }

    void retire(void* ptr, std::function<void(void*)> deleter) {
        uint64_t epoch = global_epoch_.load(std::memory_order_relaxed);
        size_t idx = epoch % NUM_EPOCHS;

        {
            std::lock_guard<std::mutex> lock(retire_lists_[idx].mtx);
            retire_lists_[idx].nodes.push_back({ptr, std::move(deleter)});
        }

        try_advance();
    }

private:
    void try_advance() {
        uint64_t current = global_epoch_.load(std::memory_order_relaxed);

        // check if all active threads have observed the current epoch
        for (size_t i = 0; i < MAX_THREADS; ++i) {
            if (!slots_[i].in_use) continue;

            uint64_t thread_epoch = slots_[i].local_epoch.load(std::memory_order_acquire);
            if (thread_epoch != UINT64_MAX && thread_epoch != current) {
                return; // someone's still in an old epoch
            }
        }

        // try to advance to next epoch via CAS (only one thread should advance)
        uint64_t new_epoch = current + 1;
        if (!global_epoch_.compare_exchange_strong(current, new_epoch,
                std::memory_order_release, std::memory_order_relaxed)) {
            return; // another thread beat us to it
        }

        // free everything from 2 epochs ago (guaranteed safe now)
        size_t old_idx = new_epoch % NUM_EPOCHS; // wraps: was (current+1) % 3 = free list for epoch before current
        std::vector<RetiredNode> to_free;

        {
            std::lock_guard<std::mutex> lock(retire_lists_[old_idx].mtx);
            to_free.swap(retire_lists_[old_idx].nodes);
        }

        for (auto& node : to_free) {
            node.deleter(node.ptr);
        }
    }
};

// RAII guard for epoch critical sections
class EpochGuard {
    EpochReclaimer& reclaimer_;
    int tid_;

public:
    EpochGuard(EpochReclaimer& reclaimer, int tid) noexcept
        : reclaimer_(reclaimer), tid_(tid) {
        reclaimer_.enter_critical(tid_);
    }

    ~EpochGuard() {
        reclaimer_.leave_critical(tid_);
    }

    EpochGuard(const EpochGuard&) = delete;
    EpochGuard& operator=(const EpochGuard&) = delete;
};

} // namespace lockless
