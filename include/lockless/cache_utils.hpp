#pragma once

#include <cstddef>
#include <type_traits>
#include <iostream>

namespace lockless {

// Cache line size for x86/x64 architectures
constexpr size_t CACHE_LINE_SIZE = 64;

// CacheAligned wrapper: ensures data sits on its own cache line
// This prevents false sharing by adding padding
template<typename T>
struct alignas(CACHE_LINE_SIZE) CacheAligned {
    T value;
    
    // Padding to fill the rest of the cache line
    static constexpr size_t padding_size = 
        (sizeof(T) % CACHE_LINE_SIZE == 0) ? 0 : (CACHE_LINE_SIZE - sizeof(T) % CACHE_LINE_SIZE);
    char padding[padding_size > 0 ? padding_size : 1];  // Ensure array is never size 0
    
    CacheAligned() = default;
    CacheAligned(const CacheAligned&) = delete;
    CacheAligned& operator=(const CacheAligned&) = delete;
    
    // Allow move for non-atomic types
    CacheAligned(CacheAligned&&) = default;
    CacheAligned& operator=(CacheAligned&&) = default;
    
    // Accessor methods (no implicit conversions to avoid issues)
    T& get() { return value; }
    const T& get() const { return value; }
    T* operator->() { return &value; }
    const T* operator->() const { return &value; }
};

// Compile-time check for alignment
template<typename T>
constexpr bool is_cache_aligned() {
    return alignof(T) >= CACHE_LINE_SIZE;
}

// Runtime utility to verify alignment
template<typename T>
bool verify_cache_alignment(const T* ptr) {
    return reinterpret_cast<uintptr_t>(ptr) % CACHE_LINE_SIZE == 0;
}

// False Sharing Detector: compile-time checks
class FalseSharingDetector {
public:
    template<typename T1, typename T2>
    static void check_separation(const T1* ptr1, const T2* ptr2, const char* name1, const char* name2) {
        uintptr_t addr1 = reinterpret_cast<uintptr_t>(ptr1);
        uintptr_t addr2 = reinterpret_cast<uintptr_t>(ptr2);
        ptrdiff_t distance = static_cast<ptrdiff_t>(addr2 > addr1 ? addr2 - addr1 : addr1 - addr2);
        
        if (distance < static_cast<ptrdiff_t>(CACHE_LINE_SIZE)) {
            std::cerr << "WARNING: Potential false sharing detected between "
                      << name1 << " (0x" << std::hex << addr1 << ") and "
                      << name2 << " (0x" << addr2 << ")" << std::dec
                      << " - distance: " << distance << " bytes" << std::endl;
        }
    }
    
    template<typename T>
    static void check_alignment(const T* ptr, const char* name) {
        if (!verify_cache_alignment(ptr)) {
            std::cerr << "WARNING: " << name << " is not cache-aligned!" << std::endl;
        }
    }
};

// Macro for easy debugging
#define CHECK_FALSE_SHARING(var1, var2) \
    lockless::FalseSharingDetector::check_separation(&var1, &var2, #var1, #var2)

#define CHECK_CACHE_ALIGNED(var) \
    lockless::FalseSharingDetector::check_alignment(&var, #var)

} // namespace lockless
