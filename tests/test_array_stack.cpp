#include "lockless/array_stack.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <set>

using namespace lockless;

void test_basic_operations() {
    std::cout << "Testing Basic Operations..." << std::endl;
    ArrayLockFreeStack<int> stack(10);
    
    assert(stack.try_push(1));
    assert(stack.try_push(2));
    assert(stack.try_push(3));
    
    int val;
    assert(stack.try_pop(val));
    assert(val == 3);
    assert(stack.try_pop(val));
    assert(val == 2);
    assert(stack.try_pop(val));
    assert(val == 1);
    
    assert(!stack.try_pop(val)); // Empty
    std::cout << "Passed." << std::endl;
}

void test_capacity() {
    std::cout << "Testing Capacity..." << std::endl;
    ArrayLockFreeStack<int> stack(2);
    
    assert(stack.try_push(1));
    assert(stack.try_push(2));
    assert(!stack.try_push(3)); // Full
    
    int val;
    assert(stack.try_pop(val));
    assert(val == 2);
    
    assert(stack.try_push(3)); // Should work now
    
    std::cout << "Passed." << std::endl;
}

void test_concurrent_stress() {
    std::cout << "Testing Concurrent Stress..." << std::endl;
    constexpr int NUM_THREADS = 8;
    constexpr int OPS_PER_THREAD = 100000;
    constexpr int STACK_SIZE = 1000; // Smaller than total ops to force contention
    
    ArrayLockFreeStack<int> stack(STACK_SIZE);
    std::atomic<int> push_count{0};
    std::atomic<int> pop_count{0};
    
    auto worker = [&](int /*id*/) {
        for (int j = 0; j < OPS_PER_THREAD; ++j) {
            if (j % 2 == 0) {
                if (stack.try_push(j)) {
                    push_count++;
                }
            } else {
                int val;
                if (stack.try_pop(val)) {
                    pop_count++;
                }
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
    
    std::cout << "Pushes: " << push_count << ", Pops: " << pop_count << std::endl;
    
    // Verify stack integrity
    int val;
    int remaining = 0;
    while (stack.try_pop(val)) {
        remaining++;
    }
    
    std::cout << "Remaining items: " << remaining << std::endl;
    assert(push_count == pop_count + remaining);
    
    std::cout << "Passed." << std::endl;
}

int main() {
    std::cout << "=== ArrayLockFreeStack Tests ===" << std::endl;
    
    ArrayLockFreeStack<int> stack(100);
    if (stack.is_lock_free()) {
        std::cout << "Stack is lock-free (Hardware supported)" << std::endl;
    } else {
        std::cout << "Warning: Stack reports not lock-free (Software fallback)" << std::endl;
    }
    
    test_basic_operations();
    test_capacity();
    test_concurrent_stress();
    
    std::cout << "All tests passed!" << std::endl;
    return 0;
}
