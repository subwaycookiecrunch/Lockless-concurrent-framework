#include "lockless/performance_monitor.hpp"
#include "lockless/ring_buffer.hpp"
#include <thread>
#include <vector>

constexpr size_t BUFFER_SIZE = 1024;
constexpr int NUM_THREADS = 4;
constexpr int OPS_PER_THREAD = 100000;

void benchmark_single_thread() {
    lockless::RingBuffer<int, BUFFER_SIZE> buffer;
    lockless::PerformanceMonitor monitor;
    
    constexpr uint64_t OPERATIONS = 1000000;
    
    monitor.start_profiling();
    
    for (uint64_t i = 0; i < OPERATIONS / 2; ++i) {
        buffer.try_push(static_cast<int>(i));
    }
    
    int val;
    for (uint64_t i = 0; i < OPERATIONS / 2; ++i) {
        buffer.try_pop(val);
    }
    
    monitor.stop_profiling(OPERATIONS);
    monitor.print_report("Ring Buffer - Single Thread");
}

void benchmark_multi_thread() {
    lockless::RingBuffer<int, BUFFER_SIZE> buffer;
    lockless::PerformanceMonitor monitor;
    
    auto producer = [&](int id) {
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            while (!buffer.try_push(id * OPS_PER_THREAD + i)) {
                std::this_thread::yield();
            }
        }
    };

    auto consumer = [&]() {
        int val;
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            while (!buffer.try_pop(val)) {
                std::this_thread::yield();
            }
        }
    };

    monitor.start_profiling();
    
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(producer, i);
        threads.emplace_back(consumer);
    }

    for (auto& t : threads) {
        t.join();
    }
    
    monitor.stop_profiling(NUM_THREADS * OPS_PER_THREAD * 2); // push + pop
    monitor.print_report("Ring Buffer - Multi Thread");
}

void benchmark_batch_operations() {
    lockless::RingBuffer<int, BUFFER_SIZE> buffer;
    lockless::PerformanceMonitor monitor;
    
    constexpr int BATCH_SIZE = 32;
    constexpr int NUM_BATCHES = 10000;
    
    int input[BATCH_SIZE];
    int output[BATCH_SIZE];
    
    for (int i = 0; i < BATCH_SIZE; ++i) {
        input[i] = i;
    }
    
    monitor.start_profiling();
    
    for (int i = 0; i < NUM_BATCHES; ++i) {
        buffer.try_push_batch(input, BATCH_SIZE);
        buffer.try_pop_batch(output, BATCH_SIZE);
    }
    
    monitor.stop_profiling(NUM_BATCHES * BATCH_SIZE * 2); // push + pop
    monitor.print_report("Ring Buffer - Batch Operations");
}

int main() {
    std::cout << "Starting Ring Buffer Benchmarks..." << std::endl;
    
    benchmark_single_thread();
    benchmark_multi_thread();
    benchmark_batch_operations();
    
    std::cout << "\nAll benchmarks completed." << std::endl;
    return 0;
}
