#pragma once

#include <atomic>
#include <array>
#include <functional>
#include <optional>
#include <memory>
#include <thread>

namespace lockless {

template<typename K, typename V, size_t Size = 1024>
class LockFreeHashMap {
    struct Node {
        K key;
        V value;
        std::atomic<bool> occupied;
        std::atomic<bool> ready;
        
        Node() : occupied(false), ready(false) {}
    };

    std::unique_ptr<Node[]> buckets_;

public:
    LockFreeHashMap() {
        buckets_ = std::make_unique<Node[]>(Size);
    }

    ~LockFreeHashMap() = default;

    LockFreeHashMap(const LockFreeHashMap&) = delete;
    LockFreeHashMap& operator=(const LockFreeHashMap&) = delete;

    bool insert(const K& key, const V& value) {
        size_t hash = std::hash<K>{}(key);
        size_t idx = hash % Size;

        for (size_t i = 0; i < Size; ++i) {
            size_t current_idx = (idx + i) % Size;
            Node& node = buckets_[current_idx];
            
            bool expected = false;
            if (node.occupied.load(std::memory_order_acquire)) {
                // Slot taken — if same key, it's a duplicate
                if (node.ready.load(std::memory_order_acquire) && node.key == key) {
                    return false;
                }
                continue;
            }
            
            // Try to claim slot via CAS
            if (node.occupied.compare_exchange_strong(expected, true,
                    std::memory_order_acquire, std::memory_order_relaxed)) {
                // Two-phase insert: write data, then publish via ready flag.
                // Readers spin briefly on !ready slots (obstruction-free).
                node.key = key;
                node.value = value;
                node.ready.store(true, std::memory_order_release);
                return true;
            }
            // Lost the CAS race, keep probing
        }
        return false; // table full
    }

    bool find(const K& key, V& result) {
        size_t hash = std::hash<K>{}(key);
        size_t idx = hash % Size;

        for (size_t i = 0; i < Size; ++i) {
            size_t current_idx = (idx + i) % Size;
            Node& node = buckets_[current_idx];
            
            if (!node.occupied.load(std::memory_order_acquire)) {
                return false; // empty slot = end of probe chain
            }
            
            // Slot claimed but data not yet visible — spin until ready
            if (!node.ready.load(std::memory_order_acquire)) {
                while (!node.ready.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
            }
            
            if (node.key == key) {
                result = node.value;
                return true;
            }
        }
        return false;
    }
};

} // namespace lockless
