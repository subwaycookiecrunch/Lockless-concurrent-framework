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
                if (node.ready.load(std::memory_order_acquire) && node.key == key) {
                    return false; // duplicate
                }
                continue;
            }
            
            // try to claim via CAS, then do a two-phase commit:
            // write data first, then flip the ready flag so readers
            // never see half-written data
            if (node.occupied.compare_exchange_strong(expected, true,
                    std::memory_order_acquire, std::memory_order_relaxed)) {
                node.key = key;
                node.value = value;
                node.ready.store(true, std::memory_order_release);
                return true;
            }
            // lost the race, keep probing
        }
        return false; // table full
    }

    bool insert(K&& key, V&& value) {
        size_t hash = std::hash<K>{}(key);
        size_t idx = hash % Size;

        for (size_t i = 0; i < Size; ++i) {
            size_t current_idx = (idx + i) % Size;
            Node& node = buckets_[current_idx];
            
            bool expected = false;
            if (node.occupied.load(std::memory_order_acquire)) {
                if (node.ready.load(std::memory_order_acquire) && node.key == key) {
                    return false;
                }
                continue;
            }
            
            if (node.occupied.compare_exchange_strong(expected, true,
                    std::memory_order_acquire, std::memory_order_relaxed)) {
                node.key = std::move(key);
                node.value = std::move(value);
                node.ready.store(true, std::memory_order_release);
                return true;
            }
        }
        return false;
    }

    bool find(const K& key, V& result) noexcept {
        size_t hash = std::hash<K>{}(key);
        size_t idx = hash % Size;

        for (size_t i = 0; i < Size; ++i) {
            size_t current_idx = (idx + i) % Size;
            Node& node = buckets_[current_idx];
            
            if (!node.occupied.load(std::memory_order_acquire)) {
                return false; // empty slot = end of probe chain
            }
            
            // slot claimed but data not written yet — spin til ready
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
