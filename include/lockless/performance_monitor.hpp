#pragma once

#include <cstdint>
#include <chrono>
#include <iostream>
#include <string>
#include <iomanip>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace lockless {

struct ProfileResult {
    uint64_t cycles;
    double seconds;
    uint64_t operations;
    
    double cycles_per_op() const {
        return operations > 0 ? static_cast<double>(cycles) / operations : 0.0;
    }
    
    double ops_per_second() const {
        return seconds > 0.0 ? operations / seconds : 0.0;
    }
};

class PerformanceMonitor {
    uint64_t start_cycles_;
    uint64_t end_cycles_;
    std::chrono::high_resolution_clock::time_point start_time_;
    std::chrono::high_resolution_clock::time_point end_time_;
    uint64_t operation_count_;
    bool profiling_;

public:
    PerformanceMonitor() : start_cycles_(0), end_cycles_(0), operation_count_(0), profiling_(false) {}

    void start_profiling() {
        profiling_ = true;
        start_time_ = std::chrono::high_resolution_clock::now();
        start_cycles_ = read_tsc();
    }

    void stop_profiling(uint64_t operations) {
        end_cycles_ = read_tsc();
        end_time_ = std::chrono::high_resolution_clock::now();
        operation_count_ = operations;
        profiling_ = false;
    }

    ProfileResult get_result() const {
        auto duration = std::chrono::duration_cast<std::chrono::duration<double>>(end_time_ - start_time_);
        return ProfileResult{
            end_cycles_ - start_cycles_,
            duration.count(),
            operation_count_
        };
    }

    void print_report(const std::string& benchmark_name) const {
        auto result = get_result();
        
        std::cout << "\n=== " << benchmark_name << " ===" << std::endl;
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Operations:       " << result.operations << std::endl;
        std::cout << "Total Cycles:     " << result.cycles << std::endl;
        std::cout << "Time Elapsed:     " << result.seconds << " seconds" << std::endl;
        std::cout << "Cycles/Op:        " << result.cycles_per_op() << std::endl;
        std::cout << std::scientific << std::setprecision(2);
        std::cout << "Throughput:       " << result.ops_per_second() << " ops/sec" << std::endl;
        std::cout << std::defaultfloat;
    }

private:
    static uint64_t read_tsc() {
#ifdef _MSC_VER
        return __rdtsc();
#elif defined(__GNUC__) || defined(__clang__)
        uint32_t lo, hi;
        __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
        return (static_cast<uint64_t>(hi) << 32) | lo;
#else
        #error "RDTSC not supported on this compiler"
#endif
    }
};

} // namespace lockless
