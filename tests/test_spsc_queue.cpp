#include "lockless/spsc_queue.hpp"
#include <iostream>
#include <thread>
#include <cassert>
#include <atomic>
#include <vector>
#include <numeric>

void test_basic() {
    lockless::SPSCQueue<int, 8> q;
    int val;

    // push until full (capacity is 7 because of sentinel slot)
    for (int i = 0; i < 7; ++i) {
        assert(q.try_push(i));
    }
    assert(!q.try_push(999)); // should be full

    // pop all
    for (int i = 0; i < 7; ++i) {
        assert(q.try_pop(val));
        assert(val == i);
    }
    assert(!q.try_pop(val)); // should be empty

    std::cout << "Basic test passed." << std::endl;
}

void test_wrap_around() {
    lockless::SPSCQueue<int, 4> q;
    int val;

    // push 3 (capacity), pop 3, push 3 again — forces wrap around
    for (int round = 0; round < 10; ++round) {
        for (int i = 0; i < 3; ++i) assert(q.try_push(round * 10 + i));
        for (int i = 0; i < 3; ++i) {
            assert(q.try_pop(val));
            assert(val == round * 10 + i);
        }
    }

    std::cout << "Wrap-around test passed." << std::endl;
}

void test_concurrent() {
    constexpr size_t QUEUE_SIZE = 1024;
    constexpr int NUM_OPS = 5000000; // 5M ops

    lockless::SPSCQueue<int, QUEUE_SIZE> q;
    std::atomic<bool> producer_done{false};
    int64_t consumer_sum = 0;

    // producer thread
    std::thread producer([&]() {
        for (int i = 0; i < NUM_OPS; ++i) {
            while (!q.try_push(i)) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    // consumer thread
    std::thread consumer([&]() {
        int val;
        int count = 0;
        int expected = 0;

        while (count < NUM_OPS) {
            if (q.try_pop(val)) {
                // FIFO: values should come out in order
                assert(val == expected);
                expected++;
                consumer_sum += val;
                count++;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    // verify sum: 0 + 1 + 2 + ... + (N-1) = N*(N-1)/2
    int64_t expected_sum = static_cast<int64_t>(NUM_OPS) * (NUM_OPS - 1) / 2;
    assert(consumer_sum == expected_sum);

    std::cout << "Concurrent test passed (5M ops, sum verified)." << std::endl;
}

void test_empty_and_capacity() {
    lockless::SPSCQueue<int, 16> q;

    assert(q.empty());
    assert(q.capacity() == 15); // one sentinel slot

    q.try_push(1);
    assert(!q.empty());

    int val;
    q.try_pop(val);
    assert(q.empty());

    std::cout << "Empty/capacity test passed." << std::endl;
}

int main() {
    std::cout << "=== SPSC Queue Tests ===" << std::endl;

    test_basic();
    test_wrap_around();
    test_empty_and_capacity();
    test_concurrent();

    std::cout << "\nAll SPSC queue tests passed!" << std::endl;
    return 0;
}
