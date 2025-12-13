#include "lockless/hash_map.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <cassert>
#include <atomic>
#include <string>

void test_basic() {
    lockless::LockFreeHashMap<int, std::string, 16> map;
    std::string val;

    assert(map.insert(1, "one"));
    assert(map.insert(2, "two"));
    assert(map.insert(3, "three"));
    
    assert(!map.insert(1, "one_again")); // Duplicate

    assert(map.find(1, val) && val == "one");
    assert(map.find(2, val) && val == "two");
    assert(map.find(3, val) && val == "three");
    assert(!map.find(4, val));

    std::cout << "HashMap Basic test passed." << std::endl;
}

void test_concurrent() {
    constexpr int NUM_THREADS = 4;
    constexpr int OPS_PER_THREAD = 1000;
    constexpr int MAP_SIZE = 8192; // Must be larger than total ops to avoid full map
    
    lockless::LockFreeHashMap<int, int, MAP_SIZE> map;
    std::atomic<int> insert_count{0};
    std::atomic<int> found_count{0};

    auto worker = [&](int id) {
        // Insert unique keys based on thread id
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            int key = id * OPS_PER_THREAD + i;
            if (map.insert(key, key * 10)) {
                insert_count++;
            }
        }
        
        // Find them back
        int val;
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            int key = id * OPS_PER_THREAD + i;
            if (map.find(key, val) && val == key * 10) {
                found_count++;
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(worker, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    assert(insert_count == NUM_THREADS * OPS_PER_THREAD);
    assert(found_count == NUM_THREADS * OPS_PER_THREAD);

    std::cout << "HashMap Concurrent test passed. Total inserts: " << insert_count << std::endl;
}

int main() {
    test_basic();
    test_concurrent();
    return 0;
}
