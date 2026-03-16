#include "lockless/epoch_reclaimer.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <cassert>
#include <atomic>

using namespace lockless;

std::atomic<int> live_objects{0};

struct TrackedObj {
    int value;
    TrackedObj(int v) : value(v) { live_objects.fetch_add(1); }
    ~TrackedObj() { live_objects.fetch_sub(1); }
};

void test_basic_retire() {
    std::cout << "Testing basic retire..." << std::endl;

    EpochReclaimer reclaimer;
    int tid = reclaimer.register_thread();
    assert(tid >= 0);

    live_objects.store(0);

    {
        EpochGuard guard(reclaimer, tid);
        auto* obj = new TrackedObj(42);
        assert(live_objects.load() == 1);
        reclaimer.retire(obj);
        // obj might still be alive here, we're in a critical section
    }

    // force a few epoch advances to trigger reclamation
    for (int i = 0; i < 5; ++i) {
        auto* dummy = new TrackedObj(i);
        reclaimer.retire(dummy);
    }

    reclaimer.unregister_thread(tid);
    std::cout << "Passed." << std::endl;
}

void test_concurrent_retire() {
    std::cout << "Testing concurrent retire..." << std::endl;

    EpochReclaimer reclaimer;
    constexpr int NUM_THREADS = 4;
    constexpr int OPS_PER_THREAD = 10000;
    std::atomic<int> total_retired{0};

    auto worker = [&](int id) {
        int tid = reclaimer.register_thread();
        assert(tid >= 0);

        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            {
                EpochGuard guard(reclaimer, tid);
                auto* obj = new TrackedObj(id * OPS_PER_THREAD + i);
                reclaimer.retire(obj);
                total_retired.fetch_add(1);
            }
            // small yield to let epochs advance
            if (i % 100 == 0) std::this_thread::yield();
        }

        reclaimer.unregister_thread(tid);
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(worker, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    assert(total_retired == NUM_THREADS * OPS_PER_THREAD);
    std::cout << "Retired " << total_retired.load() << " objects." << std::endl;
    std::cout << "Passed." << std::endl;
}

void test_guard_raii() {
    std::cout << "Testing EpochGuard RAII..." << std::endl;

    EpochReclaimer reclaimer;
    int tid = reclaimer.register_thread();

    // just make sure the guard doesn't crash or deadlock
    for (int i = 0; i < 1000; ++i) {
        EpochGuard guard(reclaimer, tid);
        auto* obj = new int(i);
        reclaimer.retire(obj);
    }

    reclaimer.unregister_thread(tid);
    std::cout << "Passed." << std::endl;
}

int main() {
    std::cout << "=== Epoch Reclaimer Tests ===" << std::endl;

    test_basic_retire();
    test_concurrent_retire();
    test_guard_raii();

    std::cout << "\nAll epoch reclaimer tests passed!" << std::endl;
    return 0;
}
