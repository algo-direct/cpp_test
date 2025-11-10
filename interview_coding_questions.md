# 40 C++ Coding Interview Questions for Hedge‑Fund / Low‑Latency Roles

Each prompt below is phrased as a coding task. Constraints and brief interviewer hints are included where useful.

1) Implement a fixed‑capacity circular buffer (ring buffer) for single‑producer single‑consumer (SPSC).
   - Constraints: lock‑free, support push/pop, O(1) operations, no dynamic allocation after construction.
   - Hint: use power‑of‑two capacity and head/tail indices with masking.

2) Implement a bounded MPMC queue (Vyukov style) with per‑slot sequence numbers.
   - Constraints: lock‑free, bounded, minimal overhead.
   - Hint: sequence numbers, acquire/release ordering, padding to avoid false sharing.

3) Write a thread‑safe fixed‑size thread pool with a work queue and graceful shutdown.
   - Constraints: efficient push/task steal optional, wakeup on submit.

4) Implement a lock‑free stack (Treiber stack) with push/pop using CAS.
   - Constraints: safe memory reclamation is not required for the simplest variant; discuss hazards.

5) Implement a simple hazard‑pointer style reclamation helper (register/unregister + retire + reclaim).
   - Constraints: correctness over performance; show basic API.

6) Implement a high‑performance LRU cache with O(1) get/put.
   - Constraints: capacity limit, thread‑safe variant optional. Use unordered_map + linked list.

7) Implement a lightweight fixed‑size slab allocator for objects of a single size.
   - Constraints: no locks in allocate/free ideally; support preallocation.

8) Implement a lock‑free single‑producer multiple‑consumer (SPMC) queue.
   - Constraints: safe concurrent pops, a single producer only.

9) Implement a token‑bucket rate limiter.
   - Constraints: allow bursts up to capacity, refill at given rate, thread‑safe.

10) Implement a compact binary parser for a simple market‑data packet (given a spec), validate CRC, and extract fields.
   - Constraints: no extra copies, handle endianness.

11) Implement an order book matching function that applies a single limit order against resting book.
   - Constraints: update levels, report trades, use efficient data structures for price levels.

12) Given N sorted streams of timestamps (vectors), merge them into a single sorted stream (k‑way merge).
   - Constraints: memory efficient, prefer a heap-based solution.

13) Implement an efficient top‑k (largest k elements) streaming algorithm.
   - Constraints: O(n log k) time, O(k) memory.

14) Implement a fast integer radix sort for 32‑bit unsigned integers.
   - Constraints: stable, O(n) expected, minimize allocations.

15) Implement a concurrent histogram collector with low contention (per-thread buckets + merge).
   - Constraints: support increment/update and periodic snapshot with minimal blocking.

16) Implement a lock‑free multi‑producer single‑consumer queue using atomics.
   - Constraints: multiple producers may call push concurrently; single consumer pop.

17) Implement a bloom filter (insert and query) and show how to choose hash functions.
   - Constraints: fixed bit array size; show false positive calculation.

18) Implement a compact, memory‑efficient representation of an order book snapshot and a function to diff two snapshots.
   - Constraints: minimize memory and CPU to compute incremental updates.

19) Implement a sliding‑window counter (e.g., requests per second over last T seconds) using buckets.
   - Constraints: constant update time, adjustable window size.

20) Implement an efficient in‑place parser that tokenizes a CSV line of numeric fields into integers/doubles without allocations.
   - Constraints: handle quoted fields and escaped commas; prefer pointer arithmetic.

21) Implement a thread‑safe priority queue optimized for many producers and one consumer.
   - Constraints: batched pushes for better throughput.

22) Implement a simple memory pool that returns aligned blocks and supports free. Show how to avoid fragmentation for fixed-size blocks.

23) Implement a consistent hashing ring with add/remove node and map key -> node.
   - Constraints: support virtual nodes for balancing.

24) Implement a compact representation of timestamps using delta encoding and write enc/dec functions.
   - Constraints: variable length encoding to minimize size when deltas are small.

25) Implement a fast function to compute VWAP (volume-weighted average price) from a stream of trades.
   - Constraints: streaming API, constant memory.

26) Implement a small fixed‑capacity histogram (HDR-like) that accumulates and reports quantiles approximately.
   - Constraints: specify error bounds and show update/query methods.

27) Implement a minimal HTTP parser that reads headers and returns method/path and a map of headers.
   - Constraints: robust to partial reads; no dynamic memory copies of header values (return views).

28) Implement a wait‑free single‑writer file appender that writes to preallocated ring of pages (mmap)
   - Constraints: handle wrap, provide durability semantics (msync optional), no locks on write path.

29) Implement a ring buffer that supports zero‑copy batch dequeue (return a contiguous block or two blocks for wrap case).
   - Constraints: avoid allocations, support batch size parameter.

30) Implement a function that detects and fixes false sharing in a struct by computing field offsets and suggesting padding locations (analysis tool).
   - Constraints: work for a simple struct description input.

31) Implement a concurrent reader–writer lock with writer preference.
   - Constraints: fairness to writers, low overhead for readers.

32) Implement a low‑latency timer wheel (single level) supporting add/cancel/expire operations.
   - Constraints: amortized O(1) insertion and expiry for typical workload; explain resolution tradeoffs.

33) Implement a highly optimized function to sum an array of floats using SIMD (fallback scalar when unavailable).
   - Constraints: use compiler intrinsics or auto-vectorization friendly code.

34) Implement a simple correctness checker that verifies a sequence of order‑book updates leads to a particular final state.
   - Constraints: process updates deterministically and validate invariants.

35) Implement a persistent append‑only log with simple indexing (segment files + index) and a reader that can tail new entries.
   - Constraints: crash-safe append (use fsync or atomic rename strategies), efficient index for lookup.

36) Implement a compact binary serialization for a small set of fields (ints, strings) and a deserializer that validates size.
   - Constraints: versioning support and backward compatibility.

37) Implement a cuckoo hash table supporting insert/find/delete with bounded relocation attempts.
   - Constraints: fixed table size and two hash functions.

38) Implement a bounded wait‑free single producer / single consumer message passing with backpressure signal using C++20 atomic wait/notify.
   - Constraints: use std::atomic::wait/notify_one for consumer/producer coordination.

39) Implement a function that takes a vector of price ticks and returns the maximum drawdown and time window in O(n).
   - Constraints: single pass, constant extra memory.

40) Implement a deterministic random sampling (reservoir sampling) for streaming data (reservoir size k).
   - Constraints: single pass over unknown total size, unbiased sample.

---

If you'd like, I can:
- Provide starter code and unit tests for any subset (I recommend picking 6–12 to turn into runnable problems).
- Provide model solutions for some of these (with explanation and complexity analysis).
