// Simple example demonstrating Ring Buffer usage
#include "lockless/ring_buffer.hpp"
#include <iostream>
#include <thread>
#include <vector>

void producer_example() {
    lockless::RingBuffer<int, 256> buffer;
    
    // Single producer
    for (int i = 0; i < 100; ++i) {
        while (!buffer.try_push(i)) {
            std::this_thread::yield();
        }
    }
    
    std::cout << "Produced 100 items" << std::endl;
}

void consumer_example() {
    lockless::RingBuffer<int, 256> buffer;
    
    // Fill buffer first
    for (int i = 0; i < 50; ++i) {
        buffer.try_push(i);
    }
    
    // Consume
    int value;
    int count = 0;
    while (buffer.try_pop(value)) {
        std::cout << "Consumed: " << value << std::endl;
        count++;
    }
    
    std::cout << "Total consumed: " << count << std::endl;
}

void mpmc_example() {
    lockless::RingBuffer<int, 1024> buffer;
    constexpr int NUM_PRODUCERS = 2;
    constexpr int NUM_CONSUMERS = 2;
    constexpr int ITEMS_PER_PRODUCER = 1000;
    
    std::vector<std::thread> threads;
    
    // Producers
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < ITEMS_PER_PRODUCER; ++j) {
                int value = i * ITEMS_PER_PRODUCER + j;
                while (!buffer.try_push(value)) {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    // Consumers
    std::atomic<int> total_consumed{0};
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        threads.emplace_back([&]() {
            int value;
            int local_count = 0;
            
            // Keep trying to consume
            while (local_count < ITEMS_PER_PRODUCER) {
                if (buffer.try_pop(value)) {
                    local_count++;
                } else {
                    std::this_thread::yield();
                }
            }
            
            total_consumed += local_count;
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "MPMC Example: Consumed " << total_consumed << " items" << std::endl;
}

int main() {
    std::cout << "=== Ring Buffer Examples ===" << std::endl;
    
    std::cout << "\n1. Producer Example:" << std::endl;
    producer_example();
    
    std::cout << "\n2. Consumer Example:" << std::endl;
    consumer_example();
    
    std::cout << "\n3. MPMC Example:" << std::endl;
    mpmc_example();
    
    return 0;
}
