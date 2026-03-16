#pragma once

#include "common.hpp"
#include <cstddef>
#include <type_traits>
#include <iostream>

namespace lockless {

// wraps T so it sits on its own cache line, prevents false sharing
template<typename T>
struct alignas(CACHE_LINE_SIZE) CacheAligned {
    T value;
    
    static constexpr size_t padding_size = 
        (sizeof(T) % CACHE_LINE_SIZE == 0) ? 0 : (CACHE_LINE_SIZE - sizeof(T) % CACHE_LINE_SIZE);
    char padding[padding_size > 0 ? padding_size : 1];
    
    CacheAligned() = default;
    CacheAligned(const CacheAligned&) = delete;
    CacheAligned& operator=(const CacheAligned&) = delete;
    
    CacheAligned(CacheAligned&&) = default;
    CacheAligned& operator=(CacheAligned&&) = default;
    
    T& get() noexcept { return value; }
    const T& get() const noexcept { return value; }
    T* operator->() noexcept { return &value; }
    const T* operator->() const noexcept { return &value; }
};

template<typename T>
constexpr bool is_cache_aligned() {
    return alignof(T) >= CACHE_LINE_SIZE;
}

template<typename T>
bool verify_cache_alignment(const T* ptr) noexcept {
    return reinterpret_cast<uintptr_t>(ptr) % CACHE_LINE_SIZE == 0;
}

// checks if two variables share a cache line — useful for debugging
class FalseSharingDetector {
public:
    template<typename T1, typename T2>
    static void check_separation(const T1* ptr1, const T2* ptr2, const char* name1, const char* name2) {
        uintptr_t addr1 = reinterpret_cast<uintptr_t>(ptr1);
        uintptr_t addr2 = reinterpret_cast<uintptr_t>(ptr2);
        ptrdiff_t distance = static_cast<ptrdiff_t>(addr2 > addr1 ? addr2 - addr1 : addr1 - addr2);
        
        if (distance < static_cast<ptrdiff_t>(CACHE_LINE_SIZE)) {
            std::cerr << "WARNING: Potential false sharing between "
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

#define CHECK_FALSE_SHARING(var1, var2) \
    lockless::FalseSharingDetector::check_separation(&var1, &var2, #var1, #var2)

#define CHECK_CACHE_ALIGNED(var) \
    lockless::FalseSharingDetector::check_alignment(&var, #var)

} // namespace lockless
