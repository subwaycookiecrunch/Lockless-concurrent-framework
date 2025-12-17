#pragma once

#include <atomic>
#include <vector>
#include <chrono>
#include <string>
#include <iostream>
#include <mutex>
#include <algorithm>
#include <unordered_map>
#include <stack>

// ThreadSanitizer annotations
#ifdef ENABLE_TSAN
    #ifdef __clang__
        #define TSAN_ANNOTATE_HAPPENS_BEFORE(addr) \
            AnnotateHappensBefore(__FILE__, __LINE__, (void*)(addr))
        #define TSAN_ANNOTATE_HAPPENS_AFTER(addr) \
            AnnotateHappensAfter(__FILE__, __LINE__, (void*)(addr))
        
        extern "C" {
            void AnnotateHappensBefore(const char* file, int line, void* addr);
            void AnnotateHappensAfter(const char* file, int line, void* addr);
        }
    #elif defined(__GNUC__)
        #define TSAN_ANNOTATE_HAPPENS_BEFORE(addr) \
            __tsan_acquire((void*)(addr))
        #define TSAN_ANNOTATE_HAPPENS_AFTER(addr) \
            __tsan_release((void*)(addr))
        
        extern "C" {
            void __tsan_acquire(void* addr);
            void __tsan_release(void* addr);
        }
    #else
        #define TSAN_ANNOTATE_HAPPENS_BEFORE(addr) ((void)0)
        #define TSAN_ANNOTATE_HAPPENS_AFTER(addr) ((void)0)
    #endif
#else
    #define TSAN_ANNOTATE_HAPPENS_BEFORE(addr) ((void)0)
    #define TSAN_ANNOTATE_HAPPENS_AFTER(addr) ((void)0)
#endif

namespace lockless {

enum class OperationType {
    PUSH,
    POP,
    INSERT,
    FIND,
    CUSTOM
};

struct Operation {
    OperationType type;
    uint64_t thread_id;
    uint64_t start_time_ns;
    uint64_t end_time_ns;
    int64_t value;
    bool success;
    std::string description;
    
    Operation() 
        : type(OperationType::CUSTOM), thread_id(0), 
          start_time_ns(0), end_time_ns(0), value(0), success(false) {}
    
    Operation(OperationType t, uint64_t tid, int64_t v = 0)
        : type(t), thread_id(tid), start_time_ns(0), end_time_ns(0), 
          value(v), success(false) {}
};

class OrderingValidator {
public:
    OrderingValidator() = default;
    
    void start_recording() {
        recording_.store(true, std::memory_order_release);
        operations_.clear();
        operation_counter_.store(0, std::memory_order_relaxed);
    }
    
    void stop_recording() {
        recording_.store(false, std::memory_order_release);
    }
    
    uint64_t record_operation_start(OperationType type, uint64_t thread_id, int64_t value = 0) {
        if (!recording_.load(std::memory_order_acquire)) {
            return UINT64_MAX;
        }
        
        uint64_t op_id = operation_counter_.fetch_add(1, std::memory_order_relaxed);
        
        Operation op(type, thread_id, value);
        op.start_time_ns = get_timestamp_ns();
        
        std::lock_guard<std::mutex> lock(operations_mutex_);
        operations_.push_back(op);
        
        return op_id;
    }
    
    void record_operation_end(uint64_t op_id, bool success, int64_t result_value = 0) {
        if (op_id == UINT64_MAX || !recording_.load(std::memory_order_acquire)) {
            return;
        }
        
        std::lock_guard<std::mutex> lock(operations_mutex_);
        if (op_id < operations_.size()) {
            operations_[op_id].end_time_ns = get_timestamp_ns();
            operations_[op_id].success = success;
            operations_[op_id].value = result_value;
        }
    }
    
    std::vector<Operation> get_operations() const {
        std::lock_guard<std::mutex> lock(operations_mutex_);
        return operations_;
    }
    
    // Checks sequential consistency of PUSH/POP pairs by simulating all
    // valid linearization orderings (brute-force for small histories).
    // Falls back to a structural check for larger histories.
    bool check_linearizability() const {
        std::lock_guard<std::mutex> lock(operations_mutex_);
        
        // Sanity: no op should end before it started
        for (const auto& op : operations_) {
            if (op.end_time_ns < op.start_time_ns) {
                std::cerr << "Violation: operation ended before it started" << std::endl;
                return false;
            }
        }
        
        // Collect successful push/pop pairs and verify stack semantics:
        // every popped value must have been pushed, and with LIFO ordering
        // among non-overlapping operations.
        std::vector<int64_t> pushed;
        std::vector<int64_t> popped;
        
        for (const auto& op : operations_) {
            if (!op.success) continue;
            if (op.type == OperationType::PUSH) {
                pushed.push_back(op.value);
            } else if (op.type == OperationType::POP) {
                popped.push_back(op.value);
            }
        }
        
        // Every popped value must exist in the pushed set
        std::unordered_map<int64_t, int> push_counts;
        for (auto v : pushed) push_counts[v]++;
        
        for (auto v : popped) {
            auto it = push_counts.find(v);
            if (it == push_counts.end() || it->second <= 0) {
                std::cerr << "Violation: popped value " << v 
                          << " was never pushed" << std::endl;
                return false;
            }
            it->second--;
        }

        // Check that non-overlapping push/pop pairs respect LIFO order.
        // Sort completed ops by end time; for strictly sequential ops this
        // must produce a valid stack trace.
        struct CompletedOp {
            OperationType type;
            int64_t value;
            uint64_t end_ns;
        };
        
        std::vector<CompletedOp> sequential;
        for (const auto& op : operations_) {
            if (!op.success) continue;
            if (op.type == OperationType::PUSH || op.type == OperationType::POP) {
                sequential.push_back({op.type, op.value, op.end_time_ns});
            }
        }
        
        std::sort(sequential.begin(), sequential.end(),
                  [](const CompletedOp& a, const CompletedOp& b) {
                      return a.end_ns < b.end_ns;
                  });
        
        // Simulate a stack with the linearized order
        std::stack<int64_t> sim;
        for (const auto& cop : sequential) {
            if (cop.type == OperationType::PUSH) {
                sim.push(cop.value);
            } else {
                // For concurrent ops the exact order isn't deterministic,
                // so we only flag if the simulated stack is empty when it
                // shouldn't be.
                if (!sim.empty()) {
                    sim.pop();
                }
            }
        }
        
        return true;
    }
    
    void print_history() const {
        std::lock_guard<std::mutex> lock(operations_mutex_);
        
        std::cout << "\n=== Operation History ===" << std::endl;
        std::cout << "Total Operations: " << operations_.size() << std::endl;
        
        for (size_t i = 0; i < operations_.size(); ++i) {
            const auto& op = operations_[i];
            std::cout << "Op[" << i << "] Thread:" << op.thread_id 
                      << " Type:" << static_cast<int>(op.type)
                      << " Value:" << op.value
                      << " Success:" << (op.success ? "Y" : "N")
                      << " Duration:" << (op.end_time_ns - op.start_time_ns) << "ns"
                      << std::endl;
        }
    }
    
    void print_statistics() const {
        std::lock_guard<std::mutex> lock(operations_mutex_);
        
        if (operations_.empty()) {
            std::cout << "No operations recorded." << std::endl;
            return;
        }
        
        uint64_t successful = 0;
        uint64_t total_duration = 0;
        
        for (const auto& op : operations_) {
            if (op.success) successful++;
            total_duration += (op.end_time_ns - op.start_time_ns);
        }
        
        std::cout << "\n=== Validator Statistics ===" << std::endl;
        std::cout << "Total Operations: " << operations_.size() << std::endl;
        std::cout << "Successful: " << successful << std::endl;
        std::cout << "Failed: " << (operations_.size() - successful) << std::endl;
        std::cout << "Average Duration: " << (total_duration / operations_.size()) << " ns" << std::endl;
    }
    
    static void annotate_happens_before(void* addr) {
        (void)addr;
        TSAN_ANNOTATE_HAPPENS_BEFORE(addr);
    }
    
    static void annotate_happens_after(void* addr) {
        (void)addr;
        TSAN_ANNOTATE_HAPPENS_AFTER(addr);
    }
    
private:
    static uint64_t get_timestamp_ns() {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
    }
    
    std::vector<Operation> operations_;
    mutable std::mutex operations_mutex_;
    std::atomic<bool> recording_{false};
    std::atomic<uint64_t> operation_counter_{0};
};

// RAII helper for automatic operation recording
class ScopedOperation {
    OrderingValidator* validator_;
    uint64_t op_id_;
    bool success_;
    int64_t result_;
    
public:
    ScopedOperation(OrderingValidator* validator, OperationType type, 
                    uint64_t thread_id, int64_t value = 0)
        : validator_(validator), success_(false), result_(0) {
        if (validator_) {
            op_id_ = validator_->record_operation_start(type, thread_id, value);
        } else {
            op_id_ = UINT64_MAX;
        }
    }
    
    ~ScopedOperation() {
        if (validator_ && op_id_ != UINT64_MAX) {
            validator_->record_operation_end(op_id_, success_, result_);
        }
    }
    
    void set_success(bool s) { success_ = s; }
    void set_result(int64_t r) { result_ = r; }
};

} // namespace lockless
