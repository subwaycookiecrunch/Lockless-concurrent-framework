# Design Decisions

Why I built things the way I did, and what I'd do differently with more time.

## Progress Guarantees Hierarchy

One of the main things I wanted to explore was the difference between lock-free and wait-free, not just in theory but in actual code and performance. The framework has three levels:

| Structure | Guarantee | Why |
|-----------|-----------|-----|
| `SPSCQueue` | Wait-free | Every operation finishes in bounded steps. No CAS loop, no retry. Just an atomic load + store. This only works because there's exactly one reader and one writer. |
| `RingBuffer` (MPMC) | Lock-free | At least one thread makes progress. Uses CAS loops that can retry under contention, but never blocks. Based on Vyukov's sequence-number scheme. |
| `ArrayLockFreeStack` | Lock-free | Same idea — CAS loop on a packed 64-bit head (tag + index). Can theoretically livelock under absurd contention, but I've never seen it happen. |

The key insight: wait-free is a stronger guarantee but it constrains your API (single producer, single consumer). Lock-free is more flexible but operations can be forced to retry.

## Why Vyukov's Queue and Not a Michael-Scott Queue?

The Michael-Scott queue is the textbook lock-free queue, but it allocates on every enqueue (it's a linked list). For the kind of work I'm targeting (low-latency message passing), allocation on the hot path is unacceptable.

Vyukov's bounded MPMC queue uses a fixed-size ring buffer with sequence numbers. Each slot has an atomic counter that tells threads whether the slot is available for writing (sequence == slot index) or reading (sequence == slot index + 1). This means:
- Zero allocations on push/pop
- Cache-friendly sequential access pattern
- Power-of-2 masking instead of modulo (faster)

The tricky part is getting the memory ordering right. The sequence load needs `acquire` to see the data written by another thread. The sequence store needs `release` to publish the data we just wrote. The head/tail CAS can be `relaxed` because the sequence numbers already provide the happens-before edges.

## The ABA Problem and Tagged Pointers

The stack originally used a pointer-based Treiber stack with `compare_exchange` on a pointer. Classic ABA problem: thread A reads top, gets preempted, thread B pops and pushes a new node at the same address, thread A wakes up and the CAS succeeds even though the stack has changed.

The standard fix is a tagged pointer — pack a monotonic counter alongside the pointer so the CAS also checks that the version hasn't changed. But on MSVC, `std::atomic` for 128-bit types (pointer + 64-bit counter) uses a lock internally. Not great for a "lock-free" data structure.

My fix: use an array-based approach. Instead of pointers, store 32-bit indices into a pre-allocated array. Pack the index + 32-bit ABA tag into a single `uint64_t`. Now the CAS is on a 64-bit value, which is natively lock-free on every platform. The downside is fixed capacity, but for my use cases that's fine.

## Epoch-Based Reclamation

This was the hardest part to get right. The problem: when thread A pops a node from the stack, it can't immediately free it because thread B might still be reading that node's `next` pointer.

I considered three approaches:

**Hazard pointers**: Each thread publishes pointers it's currently reading. Before freeing, you scan all hazard pointers. Downside: O(threads) scan on every retire, and the per-thread hazard pointer array is awkward to manage.

**Reference counting**: Atomic refcount on each node. Downside: extra cache-line bouncing on the refcount, and you need to handle the count-reaches-zero-while-incrementing race.

**Epoch-based** (what I went with): Global epoch counter. Threads entering a critical section record the current epoch. When all threads have advanced past epoch E, everything retired in epoch E is safe to free. Three epochs in rotation so there's always one "definitely safe" epoch to reclaim from.

The tradeoff: epoch-based is simpler and faster in the common case (entering/leaving a critical section is just an atomic store), but it requires that threads don't stall inside critical sections for too long. If one thread sleeps while holding an epoch, reclamation stalls for everyone. For my use case (short critical sections around queue operations), this is acceptable.

## Cache-Line Padding

False sharing is when two threads write to different variables that happen to sit on the same 64-byte cache line. The CPU's cache coherency protocol (MESI) forces bouncing the line between cores, destroying performance.

In the ring buffer, `head_` and `tail_` are the two most contended variables — producers write head, consumers write tail. If they share a cache line, every push causes the consumer's core to invalidate its cache, and vice versa. The fix is trivial: `alignas(64)` on each field. I measured a 2-3x throughput improvement from this alone in the multi-threaded benchmark.

I also added `std::hardware_destructive_interference_size` (C++17) as a fallback so the padding adapts to architectures where the cache line isn't 64 bytes (some ARM chips use 128-byte lines).

## Arena Allocator

The zero-copy queue needs shared memory that both producer and consumer can access without copying. The simplest approach is a bump allocator — just `fetch_add` the write offset.

But the original implementation had a subtle TOCTOU bug: it would `fetch_add` first, then check if the new offset overflowed. Two threads could race past the check individually but overflow together. Fixed it with a CAS loop that checks before committing.

## What I'd Change With More Time

- **Hash map needs deletion.** Linear probing without deletion is fine for the order book (unique IDs, never removed), but it's a real limitation. Tombstone markers would work but degrade probe-chain performance over time. A better approach might be Robin Hood hashing.

- **Order book matching is too simple.** A real matching engine needs price-level queues, partial fills, and ideally multi-threaded matching with a sequencer. Right now it's just a demo that ties the data structures together.

- **Benchmarks should use Google Benchmark** for statistical rigor (multiple iterations, confidence intervals, memory counters). The RDTSC approach gives rough cycle counts but doesn't account for frequency scaling.

- **Hazard pointers** for comparison with epoch-based. Would love to benchmark the two approaches head-to-head and write up the tradeoffs.
