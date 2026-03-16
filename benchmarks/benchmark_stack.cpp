#include "lockless/stack.hpp"
#include "lockless/performance_monitor.hpp"
#include <stack>
#include <mutex>
#include <iostream>
#include <thread>
#include <vector>
#include <iomanip>

using namespace lockless;

template<typename T>
inline void do_not_optimize(T const& val) {
#ifdef _MSC_VER
    volatile T sink = val;
    (void)sink;
#else
    asm volatile("" : : "r,m"(val) : "memory");
#endif
}

// mutex stack for comparison
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
    int checksum = 0;
    
    monitor.start_profiling();
    for (int i = 0; i < OPS; ++i) {
        stack.try_push(i);
    }
    int val;
    for (int i = 0; i < OPS; ++i) {
        if (stack.try_pop(val)) checksum += val;
    }
    monitor.stop_profiling(OPS * 2);
    monitor.print_report("LockFreeStack (Single Thread)");
    do_not_optimize(checksum);
}

void benchmark_mutex_stack() {
    constexpr int OPS = 1000000;
    MutexStack<int> stack;
    PerformanceMonitor monitor;
    int checksum = 0;
    
    monitor.start_profiling();
    for (int i = 0; i < OPS; ++i) {
        stack.push(i);
    }
    int val;
    for (int i = 0; i < OPS; ++i) {
        if (stack.try_pop(val)) checksum += val;
    }
    monitor.stop_profiling(OPS * 2);
    monitor.print_report("MutexStack (Single Thread)");
    do_not_optimize(checksum);
}

void benchmark_concurrent_lockfree() {
    constexpr int THREADS = 4;
    constexpr int OPS_PER_THREAD = 250000;
    LockFreeStack<int> stack(THREADS * OPS_PER_THREAD + 1000);
    PerformanceMonitor monitor;
    
    auto worker = [&](int /*id*/) {
        int local_sum = 0;
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            stack.try_push(i);
        }
        int val;
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            while(!stack.try_pop(val)) { std::this_thread::yield(); }
            local_sum += val;
        }
        do_not_optimize(local_sum);
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
        int local_sum = 0;
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            stack.push(i);
        }
        int val;
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            while(!stack.try_pop(val)) { std::this_thread::yield(); }
            local_sum += val;
        }
        do_not_optimize(local_sum);
    };
    
    monitor.start_profiling();
    std::vector<std::thread> threads;
    for (int i = 0; i < THREADS; ++i) threads.emplace_back(worker, i);
    for (auto& t : threads) t.join();
    monitor.stop_profiling(THREADS * OPS_PER_THREAD * 2);
    monitor.print_report("MutexStack (4 Threads)");
}

// scaling comparison
void benchmark_scaling() {
    const int thread_counts[] = {1, 2, 4, 8};
    constexpr int TOTAL_OPS = 400000;

    std::cout << "\n=== Stack Contention Scaling ===" << std::endl;
    std::cout << "Threads | LockFree ops/s | Mutex ops/s" << std::endl;
    std::cout << "--------|---------------|------------" << std::endl;

    for (int num_threads : thread_counts) {
        int ops_per_thread = TOTAL_OPS / num_threads;

        // lock-free
        LockFreeStack<int> lf_stack(TOTAL_OPS + 1000);
        PerformanceMonitor lf_mon;

        auto lf_worker = [&]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                lf_stack.try_push(i);
            }
            int val;
            for (int i = 0; i < ops_per_thread; ++i) {
                while (!lf_stack.try_pop(val)) std::this_thread::yield();
                do_not_optimize(val);
            }
        };

        lf_mon.start_profiling();
        std::vector<std::thread> lf_threads;
        for (int i = 0; i < num_threads; ++i) lf_threads.emplace_back(lf_worker);
        for (auto& t : lf_threads) t.join();
        lf_mon.stop_profiling(TOTAL_OPS * 2);

        // mutex
        MutexStack<int> mx_stack;
        PerformanceMonitor mx_mon;

        auto mx_worker = [&]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                mx_stack.push(i);
            }
            int val;
            for (int i = 0; i < ops_per_thread; ++i) {
                while (!mx_stack.try_pop(val)) std::this_thread::yield();
                do_not_optimize(val);
            }
        };

        mx_mon.start_profiling();
        std::vector<std::thread> mx_threads;
        for (int i = 0; i < num_threads; ++i) mx_threads.emplace_back(mx_worker);
        for (auto& t : mx_threads) t.join();
        mx_mon.stop_profiling(TOTAL_OPS * 2);

        auto lf_result = lf_mon.get_result();
        auto mx_result = mx_mon.get_result();

        std::cout << "   " << num_threads << "    | "
                  << std::fixed << std::setprecision(0)
                  << std::setw(13) << lf_result.ops_per_second() << " | "
                  << std::setw(10) << mx_result.ops_per_second() << std::endl;
    }
}

int main() {
    std::cout << "=== Stack Benchmarks ===" << std::endl;
    benchmark_lockfree_stack();
    benchmark_mutex_stack();
    benchmark_concurrent_lockfree();
    benchmark_concurrent_mutex();
    benchmark_scaling();
    return 0;
}
