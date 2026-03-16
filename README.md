# lockless

[![CI](https://github.com/subwaycookiecrunch/Lockless-concurrent-framework/actions/workflows/ci.yml/badge.svg)](https://github.com/subwaycookiecrunch/Lockless-concurrent-framework/actions/workflows/ci.yml)

bunch of lock-free data structures i wrote to actually understand memory ordering. started as me reading the Vyukov queue paper and going "ok let me just implement this myself" and then it kind of grew.

not trying to replace tbb or folly or anything, i just wanted to know why `memory_order_acquire` matters and what false sharing actually does to your throughput numbers.

see [docs/design.md](docs/design.md) for why i made specific choices.

## whats in here

- `RingBuffer<T, Size>` - bounded mpmc queue based on Vyukov's design. sequence numbers, power of 2 sizes, head/tail on separate cache lines
- `SPSCQueue<T, Size>` - wait-free single producer single consumer. no cas loops at all, just atomic loads and stores. use this if you have a dedicated producer/consumer pair
- `ArrayLockFreeStack<T>` - lock-free stack, fixed capacity. packs tag + index into a uint64 to avoid 128-bit cas (which MSVC doesnt do lock-free anyway)
- `LockFreeHashMap<K, V, Size>` - open addressing, insert-only. two-phase commit so readers never see half-written data
- `ZeroCopyQueue` - passes arena offsets through the ring buffer instead of copying. the arena uses a cas loop so two threads cant both "succeed" past the size limit
- `EpochReclaimer` + `EpochGuard` - epoch-based safe memory reclamation. basically you cant just free() a node that another thread might still be reading, this defers the free until its actually safe
- `LockFreeObjectPool` - backed by the array stack, acquire/release interface
- `OrderBook` - demo that ties everything together. not a real matching engine, just shows how the pieces fit

## building

need cmake 3.15+ and a c++20 compiler

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

## numbers

rough throughput on my machine, take with a grain of salt:

| thing | single thread | 4 threads |
|-------|-------------|-----------|
| ring buffer push+pop | ~34M ops/s | ~12M ops/s |
| lock-free stack vs mutex | 2-3x faster under contention | scales better |

run `python scripts/plot_benchmarks.py --build-dir build` after building if you want a chart of the scaling numbers.

## usage

```cpp
// mpmc
lockless::RingBuffer<int, 1024> buf;
buf.try_push(42);
int val;
buf.try_pop(val);

// wait-free spsc
lockless::SPSCQueue<int, 1024> q;
// producer thread
q.try_push(42);
// consumer thread
q.try_pop(val); // guaranteed FIFO

// epoch-based reclamation
lockless::EpochReclaimer reclaimer;
int tid = reclaimer.register_thread();
{
    lockless::EpochGuard guard(reclaimer, tid);
    auto* old_ptr = /* pop from some structure */;
    reclaimer.retire(old_ptr); // freed later when safe
}
```

## caveats

- stack's `is_lock_free()` returns false on some MSVC configs even though the algorithm doesnt use any locks. its a platform thing with how atomic<uint64_t> is implemented
- hash map cant delete or resize. fine for my use case (unique order IDs) but thats a real limitation
- the arena allocator throws when full instead of doing something smarter
- order matching is super simplified, no partial fills, no price level queues

## todo

- hash map deletion
- hazard pointer implementation to compare with epoch-based
- proper benchmarks with statistical rigor (google benchmark or similar)

## license

MIT
