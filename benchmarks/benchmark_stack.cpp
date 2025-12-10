#include "lockless/stack.hpp"
#include "lockless/performance_monitor.hpp"
#include <stack>
#include <mutex>
#include <iostream>
#include <thread>
#include <vector>

using namespace lockless;

// Standard mutex-protected stack for comparison
template<typename T>
class MutexStack {
    std::stack<T> stack_;
    std::mutex mutex_;
public:
    void push(T val) {
        std::lock_guard<std::mutex> lock(mutex_);
        stack_.push(val);
    }
    bool try_pop(T& val) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stack_.empty()) return false;
        val = stack_.top();
        stack_.pop();
        return true;
    }
};

void benchmark_lockfree_stack() {
    constexpr int OPS = 1000000;
    LockFreeStack<int> stack(OPS + 100);
    PerformanceMonitor monitor;
    
    monitor.start_profiling();
    for (int i = 0; i < OPS; ++i) {
        stack.try_push(i);
    }
    int val;
    for (int i = 0; i < OPS; ++i) {
        stack.try_pop(val);
    }
    monitor.stop_profiling(OPS * 2);
    monitor.print_report("LockFreeStack (Single Thread)");
}

void benchmark_mutex_stack() {
    constexpr int OPS = 1000000;
    MutexStack<int> stack;
    PerformanceMonitor monitor;
    
    monitor.start_profiling();
    for (int i = 0; i < OPS; ++i) {
        stack.push(i);
    }
    int val;
    for (int i = 0; i < OPS; ++i) {
        stack.try_pop(val);
    }
    monitor.stop_profiling(OPS * 2);
    monitor.print_report("MutexStack (Single Thread)");
}

void benchmark_concurrent_lockfree() {
    constexpr int THREADS = 4;
    constexpr int OPS_PER_THREAD = 250000;
    LockFreeStack<int> stack(THREADS * OPS_PER_THREAD + 1000);
    PerformanceMonitor monitor;
    
    auto worker = [&](int /*id*/) {
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            stack.try_push(i);
        }
        int val;
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            while(!stack.try_pop(val)) { std::this_thread::yield(); }
        }
    };
    
    monitor.start_profiling();
    std::vector<std::thread> threads;
    for (int i = 0; i < THREADS; ++i) threads.emplace_back(worker, i);
    for (auto& t : threads) t.join();
    monitor.stop_profiling(THREADS * OPS_PER_THREAD * 2);
    monitor.print_report("LockFreeStack (4 Threads)");
}

void benchmark_concurrent_mutex() {
    constexpr int THREADS = 4;
    constexpr int OPS_PER_THREAD = 250000;
    MutexStack<int> stack;
    PerformanceMonitor monitor;
    
    auto worker = [&](int /*id*/) {
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            stack.push(i);
        }
        int val;
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            while(!stack.try_pop(val)) { std::this_thread::yield(); }
        }
    };
    
    monitor.start_profiling();
    std::vector<std::thread> threads;
    for (int i = 0; i < THREADS; ++i) threads.emplace_back(worker, i);
    for (auto& t : threads) t.join();
    monitor.stop_profiling(THREADS * OPS_PER_THREAD * 2);
    monitor.print_report("MutexStack (4 Threads)");
}

int main() {
    std::cout << "=== Stack Benchmarks ===" << std::endl;
    benchmark_lockfree_stack();
    benchmark_mutex_stack();
    benchmark_concurrent_lockfree();
    benchmark_concurrent_mutex();
    return 0;
}
