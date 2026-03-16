#include "lockless/ring_buffer.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <cassert>
#include <atomic>
#include <set>
#include <mutex>

void test_basic() {
    lockless::RingBuffer<int, 4> buffer;
    int val;

    if(!buffer.try_push(1)) std::abort();
    if(!buffer.try_push(2)) std::abort();
    if(!buffer.try_push(3)) std::abort();
    if(!buffer.try_push(4)) std::abort();
    if(buffer.try_push(5)) std::abort(); // Full

    if(!buffer.try_pop(val) || val != 1) std::abort();
    if(!buffer.try_pop(val) || val != 2) std::abort();
    if(!buffer.try_pop(val) || val != 3) std::abort();
    if(!buffer.try_pop(val) || val != 4) std::abort();
    if(buffer.try_pop(val)) std::abort(); // Empty

    std::cout << "Basic test passed." << std::endl;
}

void test_concurrent() {
    constexpr size_t BUFFER_SIZE = 1024;
    constexpr int NUM_THREADS = 4;
    constexpr int OPS_PER_THREAD = 1000000;
    
    lockless::RingBuffer<int, BUFFER_SIZE> buffer;
    std::atomic<int> push_count{0};
    std::atomic<int> pop_count{0};

    // track all popped values per consumer to verify nothing was lost
    std::vector<std::vector<int>> per_thread_popped(NUM_THREADS);
    std::mutex pop_mutex;

    auto producer = [&](int id) {
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            int val = id * OPS_PER_THREAD + i;
            while (!buffer.try_push(val)) {
                std::this_thread::yield();
            }
            push_count++;
        }
    };

    auto consumer = [&](int consumer_id) {
        int val;
        std::vector<int> local_popped;
        local_popped.reserve(OPS_PER_THREAD);

        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            while (!buffer.try_pop(val)) {
                std::this_thread::yield();
            }
            local_popped.push_back(val);
            pop_count++;
        }

        std::lock_guard<std::mutex> lock(pop_mutex);
        per_thread_popped[consumer_id] = std::move(local_popped);
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(producer, i);
        threads.emplace_back(consumer, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    assert(push_count == NUM_THREADS * OPS_PER_THREAD);
    assert(pop_count == NUM_THREADS * OPS_PER_THREAD);

    // verify every pushed value was popped exactly once
    std::set<int> all_popped;
    for (auto& vec : per_thread_popped) {
        for (int v : vec) {
            auto [it, inserted] = all_popped.insert(v);
            assert(inserted); // no duplicates
        }
    }
    assert(static_cast<int>(all_popped.size()) == NUM_THREADS * OPS_PER_THREAD);

    std::cout << "Concurrent test passed (" << push_count + pop_count << " total ops)." << std::endl;
}

int main() {
    test_basic();
    test_concurrent();
    
    // Batch Test
    {
        lockless::RingBuffer<int, 8> buffer;
        int input[] = {1, 2, 3, 4, 5};
        int output[5];

        if(buffer.try_push_batch(input, 5) != 5) std::abort();
        if(buffer.try_pop_batch(output, 5) != 5) std::abort();
        
        for(int i=0; i<5; ++i) if(output[i] != i+1) std::abort();
        
        int input2[] = {6, 7, 8, 9, 10};
        if(buffer.try_push_batch(input2, 5) != 5) std::abort();
        if(buffer.try_push_batch(input2, 5) != 3) std::abort();
        
        std::cout << "Batch test passed." << std::endl;
    }
    return 0;
}
