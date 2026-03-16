#include "lockless/performance_monitor.hpp"
#include "lockless/ring_buffer.hpp"
#include <thread>
#include <vector>
#include <iostream>

// prevent the compiler from optimizing away benchmark values
template<typename T>
inline void do_not_optimize(T const& val) {
    // volatile read + assembly clobber prevents dead-code elimination
#ifdef _MSC_VER
    volatile T sink = val;
    (void)sink;
#else
    asm volatile("" : : "r,m"(val) : "memory");
#endif
}

constexpr size_t BUFFER_SIZE = 1024;
constexpr int NUM_THREADS = 4;
constexpr int OPS_PER_THREAD = 100000;

void benchmark_single_thread() {
    lockless::RingBuffer<int, BUFFER_SIZE> buffer;
    lockless::PerformanceMonitor monitor;
    
    constexpr uint64_t OPERATIONS = 1000000;
    int checksum = 0;
    
    monitor.start_profiling();
    
    for (uint64_t i = 0; i < OPERATIONS / 2; ++i) {
        buffer.try_push(static_cast<int>(i));
    }
    
    int val;
    for (uint64_t i = 0; i < OPERATIONS / 2; ++i) {
        if (buffer.try_pop(val)) checksum += val;
    }
    
    monitor.stop_profiling(OPERATIONS);
    monitor.print_report("Ring Buffer - Single Thread");
    do_not_optimize(checksum);
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
        int local_sum = 0;
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            while (!buffer.try_pop(val)) {
                std::this_thread::yield();
            }
            local_sum += val;
        }
        do_not_optimize(local_sum);
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
    
    monitor.stop_profiling(NUM_THREADS * OPS_PER_THREAD * 2);
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
    
    int checksum = 0;
    
    monitor.start_profiling();
    
    for (int i = 0; i < NUM_BATCHES; ++i) {
        buffer.try_push_batch(input, BATCH_SIZE);
        size_t n = buffer.try_pop_batch(output, BATCH_SIZE);
        for (size_t j = 0; j < n; ++j) checksum += output[j];
    }
    
    monitor.stop_profiling(NUM_BATCHES * BATCH_SIZE * 2);
    monitor.print_report("Ring Buffer - Batch Operations");
    do_not_optimize(checksum);
}

// scaling benchmark — shows how throughput changes with contention
void benchmark_scaling() {
    const int thread_counts[] = {1, 2, 4, 8};
    constexpr int TOTAL_OPS = 400000;

    std::cout << "\n=== Contention Scaling ===" << std::endl;
    std::cout << "Threads | Ops/sec" << std::endl;
    std::cout << "--------|--------" << std::endl;

    for (int num_threads : thread_counts) {
        lockless::RingBuffer<int, 4096> buffer;
        lockless::PerformanceMonitor monitor;

        int ops_per_thread = TOTAL_OPS / num_threads;

        auto worker = [&]() {
            int val;
            for (int i = 0; i < ops_per_thread; ++i) {
                while (!buffer.try_push(i)) std::this_thread::yield();
                while (!buffer.try_pop(val)) std::this_thread::yield();
                do_not_optimize(val);
            }
        };

        monitor.start_profiling();

        std::vector<std::thread> threads;
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back(worker);
        }
        for (auto& t : threads) t.join();

        monitor.stop_profiling(TOTAL_OPS * 2);
        auto result = monitor.get_result();

        std::cout << "   " << num_threads << "    | " 
                  << std::fixed << std::setprecision(0)
                  << result.ops_per_second() << std::endl;
    }
}

int main() {
    std::cout << "Starting Ring Buffer Benchmarks..." << std::endl;
    
    benchmark_single_thread();
    benchmark_multi_thread();
    benchmark_batch_operations();
    benchmark_scaling();
    
    std::cout << "\nAll benchmarks completed." << std::endl;
    return 0;
}
