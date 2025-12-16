#include "lockless/cache_utils.hpp"
#include <iostream>
#include <atomic>
#include <cstdint>

void test_cache_aligned_basic() {
    lockless::CacheAligned<int> aligned_int;
    aligned_int.get() = 42;
    
    // Check alignment
    if (lockless::verify_cache_alignment(&aligned_int)) {
        std::cout << "✓ CacheAligned<int> is properly aligned" << std::endl;
    } else {
        std::cerr << "✗ CacheAligned<int> alignment failed!" << std::endl;
        std::abort();
    }
    
    // Check value access
    if (aligned_int.get() != 42) {
        std::cerr << "✗ Value access failed!" << std::endl;
        std::abort();
    }
    
    aligned_int.get() = 100;
    if (aligned_int.get() != 100) {
        std::cerr << "✗ Assignment failed!" << std::endl;
        std::abort();
    }
    
    std::cout << "✓ CacheAligned basic operations passed" << std::endl;
}

void test_cache_aligned_atomic() {
    lockless::CacheAligned<std::atomic<uint64_t>> aligned_atomic1;
    lockless::CacheAligned<std::atomic<uint64_t>> aligned_atomic2;
    
    // Check alignment of both
    CHECK_CACHE_ALIGNED(aligned_atomic1);
    CHECK_CACHE_ALIGNED(aligned_atomic2);
    
    // Check they are on separate cache lines
    CHECK_FALSE_SHARING(aligned_atomic1, aligned_atomic2);
    
    uintptr_t addr1 = reinterpret_cast<uintptr_t>(&aligned_atomic1);
    uintptr_t addr2 = reinterpret_cast<uintptr_t>(&aligned_atomic2);
    ptrdiff_t distance = static_cast<ptrdiff_t>(addr2 - addr1);
    
    if (distance >= static_cast<ptrdiff_t>(lockless::CACHE_LINE_SIZE)) {
        std::cout << "✓ Atomics are on separate cache lines (distance: " << distance << " bytes)" << std::endl;
    }
    
    std::cout << "✓ CacheAligned atomic tests passed" << std::endl;
}

void test_sizeof() {
    std::cout << "\nSize Information:" << std::endl;
    std::cout << "sizeof(int): " << sizeof(int) << " bytes" << std::endl;
    std::cout << "sizeof(CacheAligned<int>): " << sizeof(lockless::CacheAligned<int>) << " bytes" << std::endl;
    std::cout << "sizeof(std::atomic<uint64_t>): " << sizeof(std::atomic<uint64_t>) << " bytes" << std::endl;
    std::cout << "sizeof(CacheAligned<std::atomic<uint64_t>>): " 
              << sizeof(lockless::CacheAligned<std::atomic<uint64_t>>) << " bytes" << std::endl;
    std::cout << "CACHE_LINE_SIZE: " << lockless::CACHE_LINE_SIZE << " bytes" << std::endl;
}

void test_compile_time_checks() {
    static_assert(lockless::is_cache_aligned<lockless::CacheAligned<int>>(), 
                  "CacheAligned<int> must be cache-aligned");
    static_assert(lockless::is_cache_aligned<lockless::CacheAligned<std::atomic<uint64_t>>>(), 
                  "CacheAligned<atomic> must be cache-aligned");
    
    std::cout << "✓ Compile-time alignment checks passed" << std::endl;
}

int main() {
    std::cout << "=== Cache Utils Tests ===" << std::endl;
    
    test_compile_time_checks();
    test_cache_aligned_basic();
    test_cache_aligned_atomic();
    test_sizeof();
    
    std::cout << "\nAll cache utils tests passed!" << std::endl;
    return 0;
}
