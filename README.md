# Lockless

Lock-free data structures for C++20. Started as a learning project to understand memory ordering and CAS loops, turned into something actually usable for low-latency work.

The core idea: bounded MPMC ring buffer (based on Vyukov's design), a Treiber-style stack with tagged-pointer ABA protection, and a simple insert-only hash map. On top of that there's a toy order book that ties the pieces together, some cache-line utilities, and a basic correctness checker.

Not trying to compete with Intel TBB or Folly — this is more of a from-scratch exercise to deeply understand why `memory_order_acquire` matters and how false sharing kills throughput.

## What's in here

**Data structures**
- `RingBuffer<T, Size>` — Bounded MPMC queue (Vyukov). Sequence-number based, power-of-2 sizes.
- `ArrayLockFreeStack<T>` — Fixed-capacity lock-free stack. Uses 64-bit tagged index (32-bit tag + 32-bit index) to avoid 16-byte CAS on MSVC.
- `LockFreeHashMap<K, V, Size>` — Open-addressing with linear probing. Insert-only (no delete). Two-phase commit via occupied/ready flags.
- `ZeroCopyQueue` — Passes handles into a shared arena instead of copying data through the queue.

**Utilities**
- `CacheAligned<T>` — Wrapper that forces 64-byte alignment to prevent false sharing.
- `FalseSharingDetector` — Runtime check for cache-line proximity between atomics.
- `OrderingValidator` — Records concurrent operations and checks sequential consistency of push/pop pairs.
- `PerformanceMonitor` — RDTSC-based cycle counter with throughput reporting.

**Applications**
- `OrderBook` — Lock-free order submission via ring buffer, single-threaded matching engine. Tracks best bid/ask with atomic CAS loops.
- `LockFreeObjectPool` — Acquire/release pool backed by the array stack.

## Building

Needs a C++20 compiler (tested on MSVC 19.x, GCC 12+, Clang 15+) and CMake 3.15+.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

For ThreadSanitizer (GCC/Clang only):
```bash
cmake -S . -B build_tsan -DENABLE_TSAN=ON
cmake --build build_tsan
```

## Some numbers

Ring buffer on my machine (Ryzen 7, MSVC, Release):

| Scenario | Operations | Cycles/Op | Throughput |
|----------|-----------|-----------|------------|
| Single thread | 1M | ~59 | 34M ops/s |
| 4 producers + 4 consumers | 800K | ~162 | 12M ops/s |
| Batch (32-element chunks) | 640K | ~107 | 18M ops/s |

Order book: ~4000 orders from 4 threads, ~49% match rate, zero contention on the submission queue.

These are rough — run the benchmarks yourself to get numbers for your hardware.

## Usage

```cpp
#include "lockless/ring_buffer.hpp"

lockless::RingBuffer<int, 1024> buf;

buf.try_push(42);

int val;
if (buf.try_pop(val)) {
    // got 42
}
```

```cpp
#include "lockless/stack.hpp"

lockless::LockFreeStack<int> stack(1000);

stack.try_push(1);
stack.try_push(2);

int v;
stack.try_pop(v); // v == 2 (LIFO)
```

See `/examples` for more.

## Project layout

```
include/lockless/
    ring_buffer.hpp
    array_stack.hpp
    stack.hpp              (alias for ArrayLockFreeStack)
    hash_map.hpp
    message_queue.hpp
    cache_utils.hpp
    ordering_validator.hpp
    performance_monitor.hpp
    order_book.hpp
    object_pool.hpp
tests/
    test_ring_buffer.cpp
    test_stack.cpp
    test_array_stack.cpp
    test_hash_map.cpp
    test_message_queue.cpp
    test_cache_utils.cpp
    test_ordering_validator.cpp
    test_order_book.cpp
    test_object_pool.cpp
benchmarks/
    benchmark_ring_buffer.cpp
    benchmark_stack.cpp
    benchmark_order_book.cpp
examples/
    ring_buffer_example.cpp
    order_book_example.cpp
CMakeLists.txt
```

## Known issues

- The stack reports `is_lock_free() == false` on MSVC because `std::atomic<uint64_t>` uses a lock internally on some configs. It still works correctly, just not technically lock-free on that platform.
- Hash map is insert-only — no deletion or resize. Fine for the order book use case (unique order IDs) but limited otherwise.
- Order book matching is simplified: single-threaded processor, no partial fills, no price-level queues.
- The zero-copy arena is a linear bump allocator that throws when full. Not suitable for long-running producers without manual reset.

## TODO

- Hazard pointers or epoch-based reclamation for safe memory management
- Hash map deletion support
- Multi-threaded matching with a sequencer pattern
- Better arena allocator (ring of blocks instead of linear)

## License

MIT
