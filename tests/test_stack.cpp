#include "lockless/stack.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <cassert>
#include <atomic>

using namespace lockless;

void test_basic_operations() {
    std::cout << "Testing Basic Operations..." << std::endl;
    LockFreeStack<int> stack(100); 
    
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

void test_concurrent_push_pop() {
    std::cout << "Testing Concurrent Push/Pop..." << std::endl;
    constexpr int NUM_OPS = 100000;
    LockFreeStack<int> stack(NUM_OPS * 2 + 100); 
    std::atomic<int> sum{0};
    
    auto push_worker = [&]() {
        for (int i = 0; i < NUM_OPS; ++i) {
            while(!stack.try_push(i)) { std::this_thread::yield(); }
        }
    };
    
    auto pop_worker = [&]() {
        int val;
        for (int i = 0; i < NUM_OPS; ++i) {
            while (!stack.try_pop(val)) { std::this_thread::yield(); }
            sum += val;
        }
    };
    
    std::thread t1(push_worker);
    std::thread t2(push_worker);
    std::thread t3(pop_worker);
    std::thread t4(pop_worker);
    
    t1.join();
    t2.join();
    t3.join();
    t4.join();
    
    // Sum of 0..999 is 499500. Two pushers = 999000.
    int expected = (NUM_OPS * (NUM_OPS - 1)) / 2 * 2;
    std::cout << "Sum: " << sum << " (Expected: " << expected << ")" << std::endl;
    assert(sum == expected);
    std::cout << "Passed." << std::endl;
}

int main() {
    std::cout << "=== LockFreeStack Tests (Array-Based) ===" << std::endl;
    
    LockFreeStack<int> stack(10);
    if (stack.is_lock_free()) {
        std::cout << "Stack is lock-free" << std::endl;
    } else {
        std::cout << "Stack is NOT lock-free" << std::endl;
    }
    
    test_basic_operations();
    test_concurrent_push_pop();
    
    std::cout << "All tests passed!" << std::endl;
    return 0;
}
