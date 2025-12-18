#include "lockless/object_pool.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>

using namespace lockless;

struct TestObject {
    int id;
    char padding[60]; // Make it cache-line sized-ish
};

void test_basic_pool() {
    std::cout << "Testing Basic Pool..." << std::endl;
    LockFreeObjectPool<TestObject> pool(10);
    
    TestObject* obj1 = pool.acquire();
    assert(obj1 != nullptr);
    obj1->id = 1;
    
    TestObject* obj2 = pool.acquire();
    assert(obj2 != nullptr);
    obj2->id = 2;
    
    assert(obj1 != obj2);
    
    pool.release(obj1);
    pool.release(obj2);
    
    // Should be able to re-acquire
    TestObject* obj3 = pool.acquire();
    assert(obj3 != nullptr);
    
    std::cout << "Passed." << std::endl;
}

void test_pool_exhaustion() {
    std::cout << "Testing Pool Exhaustion..." << std::endl;
    LockFreeObjectPool<int> pool(2);
    
    int* p1 = pool.acquire();
    int* p2 = pool.acquire();
    int* p3 = pool.acquire();
    
    assert(p1 != nullptr);
    assert(p2 != nullptr);
    assert(p3 == nullptr); // Full
    
    pool.release(p1);
    p3 = pool.acquire();
    assert(p3 != nullptr); // Should work now
    assert(p3 == p1); // Should reuse the slot (LIFO)
    
    std::cout << "Passed." << std::endl;
}

void test_concurrent_pool() {
    std::cout << "Testing Concurrent Pool..." << std::endl;
    constexpr int NUM_THREADS = 8;
    constexpr int OPS_PER_THREAD = 50000;
    constexpr int POOL_SIZE = 100; // Much smaller than total threads * ops
    
    LockFreeObjectPool<int> pool(POOL_SIZE);
    std::atomic<int> success_count{0};
    
    auto worker = [&](int /*id*/) {
        for (int j = 0; j < OPS_PER_THREAD; ++j) {
            int* ptr = pool.acquire();
            if (ptr) {
                *ptr = j; // Use the memory
                pool.release(ptr);
                success_count++;
            } else {
                // Pool empty, just yield
                std::this_thread::yield();
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
    
    std::cout << "Successful acquisitions: " << success_count << std::endl;
    std::cout << "Passed." << std::endl;
}

int main() {
    std::cout << "=== LockFreeObjectPool Tests ===" << std::endl;
    
    test_basic_pool();
    test_pool_exhaustion();
    test_concurrent_pool();
    
    std::cout << "All tests passed!" << std::endl;
    return 0;
}
