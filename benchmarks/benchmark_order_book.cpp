#include "lockless/order_book.hpp"
#include "lockless/performance_monitor.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <random>

using namespace lockless;

void benchmark_order_submission() {
    OrderBook<8192> book;
    PerformanceMonitor monitor;
    
    // Must be less than buffer size (8192) to avoid blocking
    constexpr uint64_t NUM_ORDERS = 8000;
    
    monitor.start_profiling();
    
    for (uint64_t i = 0; i < NUM_ORDERS; ++i) {
        Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        uint64_t price = 10000 + (i % 200);
        Order order(i, side, price, 100, i);
        
        while (!book.submit_order(order)) {
            // Should not happen if NUM_ORDERS < 8192
            std::this_thread::yield();
        }
    }
    
    monitor.stop_profiling(NUM_ORDERS);
    monitor.print_report("Order Submission - Single Thread");
}

void benchmark_order_processing() {
    OrderBook<8192> book;
    
    // Pre-fill the book (must be < 8192)
    constexpr uint64_t NUM_ORDERS = 8000;
    for (uint64_t i = 0; i < NUM_ORDERS; ++i) {
        Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        uint64_t price = 10000 + (i % 200);
        Order order(i, side, price, 100, i);
        
        while (!book.submit_order(order)) {
            std::this_thread::yield();
        }
    }
    
    // Now benchmark processing
    PerformanceMonitor monitor;
    monitor.start_profiling();
    
    uint64_t total_processed = 0;
    while (true) {
        uint64_t processed = book.process_orders(100);
        total_processed += processed;
        if (processed == 0 && total_processed >= NUM_ORDERS) break;
        if (processed == 0) std::this_thread::yield();
    }
    
    monitor.stop_profiling(total_processed);
    monitor.print_report("Order Processing - Matching Engine");
    
    std::cout << "  Matched Orders: " << book.get_matched_orders() << std::endl;
}

void benchmark_concurrent_submission() {
    OrderBook<8192> book;
    PerformanceMonitor monitor;
    
    constexpr int NUM_THREADS = 4;
    constexpr int ORDERS_PER_THREAD = 25000;
    constexpr int TOTAL_ORDERS = NUM_THREADS * ORDERS_PER_THREAD;
    
    std::atomic<bool> running{true};
    
    // Consumer thread to drain the queue
    std::thread consumer([&]() {
        while (running) {
            book.process_orders(100);
            std::this_thread::yield();
        }
        // Process a bit more to ensure we don't leave too much pending
        for (int i = 0; i < 100; ++i) {
             book.process_orders(100);
        }
    });
    
    auto submit_orders = [&](int thread_id) {
        std::mt19937 rng(thread_id);
        std::uniform_int_distribution<uint64_t> price_dist(9900, 10100);
        std::uniform_int_distribution<uint64_t> qty_dist(10, 100);
        
        for (int i = 0; i < ORDERS_PER_THREAD; ++i) {
            uint64_t order_id = thread_id * ORDERS_PER_THREAD + i;
            Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
            uint64_t price = price_dist(rng);
            uint64_t quantity = qty_dist(rng);
            
            Order order(order_id, side, price, quantity, order_id);
            
            while (!book.submit_order(order)) {
                std::this_thread::yield();
            }
        }
    };
    
    monitor.start_profiling();
    
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(submit_orders, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    monitor.stop_profiling(TOTAL_ORDERS);
    monitor.print_report("Order Submission - Multi Thread");
    
    running = false;
    consumer.join();
    
    std::cout << "  Matched Orders: " << book.get_matched_orders() << std::endl;
}

int main() {
    std::cout << "Starting Order Book Benchmarks..." << std::endl;
    
    benchmark_order_submission();
    benchmark_order_processing();
    benchmark_concurrent_submission();
    
    std::cout << "\nAll benchmarks completed." << std::endl;
    return 0;
}
