# Hedge‑Fund C++ Interview Questions — 40 Q&A

This document contains 40 interview questions commonly asked for hedge‑fund software engineer roles that focus on C++, low‑latency systems, and production reliability. Each question includes a concise, practical answer and interviewer hints where useful.

---

1) Question: Explain RAII and give an example where RAII prevents resource leaks.

Answer: RAII (Resource Acquisition Is Initialization) ties resource lifetime to object lifetime: acquire in constructor, release in destructor. In C++ this yields deterministic cleanup even when exceptions are thrown. Example: use std::unique_ptr<T[]> for dynamic arrays or a custom file descriptor wrapper that closes the FD in its destructor. Interviewers expect discussion of exception safety (strong/weak/no‑throw guarantees) and how RAII composes with move semantics; show sample code with RAII wrapper for a FILE* or socket.

---

2) Question: Describe move semantics and when to implement move constructors/assignments.

Answer: Move semantics allow transfer of resources from temporaries without copying. Implement move ctor/assignment when your type owns heap memory, file descriptors, or other unique resources and moving is cheap relative to copying. Mark move operations noexcept when possible (enables optimizations in containers). Show std::move usage and the “copy‑and‑swap” idiom for strong exception safety in assignment when moves might throw.

---

3) Question: How do you avoid data races? Explain memory_order semantics in std::atomic.

Answer: Avoid data races by synchronization: mutexes, condition variables, and atomics. For atomics, memory_order_seq_cst is the default (strongest); acquire/release pairs give a lighter ordering useful for many patterns (release on producer, acquire on consumer). memory_order_relaxed allows atomic reads/writes without ordering, useful for counters. Understand fences, happens‑before, and when to use atomic read‑modify‑write (CAS). Interviewers want examples: lock‑free queue head/tail using acquire/release and why relaxed is safe for independent counters.

---

4) Question: Implement or explain a lock‑free SPSC queue and the Vyukov MPMC queue pattern.

Answer: SPSC ring buffer: fixed-length array, producer owns head index, consumer owns tail; only one writer and one reader so no CAS required. Vyukov MPMC uses per-slot sequence numbers and CAS on head/tail with careful memory orderings; each cell holds a sequence value and data; producers and consumers spin until seq indicates the slot is ready. Discuss false sharing, power‑efficient spinning (pause/yield/sleep), and ABI‑portable signed diffs. Interviewers expect awareness of ABA, padding, and how to choose capacity as power of two for masking.

---

5) Question: What is false sharing and how do you mitigate it?

Answer: False sharing is when unrelated variables on the same cache line are modified by different cores, causing excessive cache coherency traffic. Mitigation: align hot variables to cache line boundaries (alignas), add padding to separate fields, or reorganize data so per-thread counters live in separate cache lines. Also profile to confirm false sharing before adding padding. On Linux, perf or VTune can reveal coherence misses.

---

6) Question: How do you profile a latency‑sensitive C++ program? Which tools and metrics matter?

Answer: Use perf, VTune, or Linux perf events to measure cycles, instructions, cache hits/misses, and branch misses. Collect flamegraphs to locate hotspots; use sampling and event counters to find tail latency contributors. Measure distribution (P50/P95/P99/P99.9) rather than just mean. For microbenchmarks, use cycle counters (rdtsc carefully with serialization) and ensure realistic load and CPU affinity.

---

7) Question: Differences between malloc/free and operator new/delete and where allocators fit.

Answer: malloc/free are C allocation routines returning raw memory; new/delete call constructors/destructors and may use operator new which can be overloaded. Custom allocators (std::pmr, custom pools) reduce contention, fragmentation, and per‑allocation syscall overhead. For trading systems, slab allocators, thread‑local pools, or bump allocators are common. Discuss alignment and when using preallocated arenas or huge pages helps.

---

8) Question: Design a low‑latency market‑data handler that parses and dispatches messages to multiple threads.

Answer: Key points: prefer zero copy, parse in place, batch processing, pin threads to cores, and use per‑consumer queues to avoid locking hot paths. Apply backpressure: drop or coalesce messages if consumers lag, or use bounded queues with monitoring. Use memory pools for message objects and avoid heap allocations in the hot path. Consider kernel bypass (DPDK) or specialized NIC features for the lowest latencies.

---

9) Question: How would you implement an order book? Data structures and layout?

Answer: Typical structure: price levels keyed by price, each level holds size and orders (linked list or vector). Use balanced trees (std::map) for correctness, but arrays/skip lists or price‑indexed buckets can be faster if price space is bounded. For high performance, separate price levels and order entries to avoid pointer overhead and tune memory layout for locality. Discuss time/price priority and how matching engine must be deterministic and thread‑safe.

---

10) Question: Explain branch prediction and how to reduce mispredictions.

Answer: Branch predictors guess the likely path; mispredictions cost cycles to flush pipelines. Strategies: write predictable branches, use branchless code (ternary ops, conditional moves), reorder checks to handle the common case first, use profile‑guided optimization (PGO), and use lookup tables. Measure mispredictions with perf and rewrite hot loops to avoid unpredictable branches.

---

11) Question: What is a memory barrier? Give an example where it's needed.

Answer: Memory barriers (fences) prevent CPU or compiler reordering. Example: implementing a producer/consumer ring buffer where producer writes data then sets a flag — use release on the flag store and acquire on the flag load to ensure writes are visible to the consumer before it reads the flag. Explain compiler vs CPU fences and atomic operations that imply ordering.

---

12) Question: How do you write deterministic tests for multi‑threaded code?

Answer: Use injected scheduling points (hooks to yield or block), deterministic thread scheduling frameworks, or mock timing to force interleavings. Use logging with vector clocks or event tracing to reproduce sequences; combine with stress tests and sanitizers (TSan) to catch races. For lock‑free code, unit test by checking invariants under controlled concurrency and use model checkers when possible.

---

13) Question: How would you measure and reduce tail latency for a trading path?

Answer: Measure histograms (HDR histogram), log P50/P95/P99/P99.9 and investigate sources: GC pauses, interrupts, context switches, high CPU load, cache misses. Reduce jitter with CPU pinning, isolate cores, tune NIC interrupts (NAPI/IRQ affinity), avoid dynamic memory allocations on hot path, and use busy‑polling where appropriate. Also tune OS (disable ASLR, use hugepages) and ensure NUMA locality.

---

14) Question: Discuss integer overflow, floating‑point precision, and numerical issues in finance.

Answer: Prefer fixed‑point or integer cents representation for money to avoid rounding issues. For large products, use int128 or checked math to avoid overflow. Floating point has precision and rounding mode issues; use decimal libraries for requirements needing exact decimal arithmetic. Interviewers look for awareness of reproducibility across platforms and proper error handling for edge cases.

---

15) Question: Explain cpu affinity, huge pages, and kernel bypass (DPDK) for latency.

Answer: Cpu affinity pins threads to cores, reducing migration and cache warmup. Huge pages reduce TLB pressure and page table overhead improving latency for large memory footprints. Kernel bypass (DPDK) moves packet handling into user space and polls NIC rings to avoid interrupt latency; it requires careful NUMA and memory setup and is more complex but yields microsecond‑class latency.

---

16) Question: compare compare_exchange_weak vs compare_exchange_strong and use cases.

Answer: compare_exchange_weak may fail spuriously (recommended in loops for performance on some architectures); compare_exchange_strong only fails on real inequality. Use weak inside a loop for typical CAS patterns. Understand the memory ordering parameters and how to write a correct retry loop.

---

17) Question: How do you prevent priority inversion or starvation in a multi‑threaded system?

Answer: Use fair locks (e.g., ticket locks) where needed, bounded queues with backpressure, avoid long‑running critical sections, and consider priority inheritance if real-time priorities are in use. For user‑level scheduling, design cooperative work stealing with work quotas to reduce starvation.

---

18) Question: Implement a high‑performance timer wheel or priority queue for scheduled events.

Answer: Timer wheel (hierarchical or hashed) buckets timers into slots to get O(1) amortized insertion and expiry; choose resolution and number of levels based on max timer window. For small scales, binary heap (std::priority_queue) is fine; for large numbers of timers where many have similar expiry, a wheel is better. Discuss complexity, memory layout, and cancellation cost.

---

19) Question: Why ABI/versioning and build reproducibility matter in production trading systems?

Answer: ABI/SONAME changes can break deployed services; deterministic builds help reproduce bugs and security issues. Use symbol visibility controls, semantic versioning, CI with pinned toolchains and reproducible flags, and containers or build artifacts to ensure identical binaries across environments. Discuss link-time optimization (LTO) and its implications for ABI and debugging.

---

20) Question: Walk through debugging a production crash with a core dump and no live debugger.

Answer: Collect core, exact binary, and symbol files (strip separate), then use gdb to inspect backtraces and memory. Check /proc/<pid>/maps equivalent at crash time if logged, examine threads and registers, find the crashing instruction and analyze call stack and local variables. Use ASAN/UBsan builds to reproduce and add lightweight logging or assertions to narrow cause.

---

21) Question: Explain AddressSanitizer (ASan) and ThreadSanitizer (TSan) and when to use them.

Answer: ASan detects heap/stack/out‑of‑bounds and use‑after‑free; TSan detects data races. Use ASan in debug/test builds to catch memory errors; TSan is heavy but invaluable for race detection during testing. These tools have runtime overhead and can change timing; use smaller reproducing tests and inspect stack traces they provide.

---

22) Question: How do you design a custom allocator for low latency?

Answer: Techniques: per‑thread caches to avoid locks, slab allocators for fixed‑size objects, free lists with ABA protection, and memory pools with prewarming. Avoid system calls on hot path; use mmap/hugepages for large allocations; ensure alignment. Measure fragmentation and throughput and expose metrics for tuning.

---

23) Question: Explain the C++ memory model and the difference between relaxed and sequentially consistent atomics.

Answer: The C++ memory model defines happens‑before and the guarantees for atomic operations. Sequential consistency gives a global total order consistent with program order. Relaxed atomics only guarantee atomicity, no ordering; use for counters where ordering doesn’t matter. Acquire/release provide synchronization for producer/consumer relationships.

---

24) Question: What are the tradeoffs between lock‑based and lock‑free designs?

Answer: Lock‑based code is simpler and less error‑prone; locks can cause contention, priority inversion, and blocking. Lock‑free is nonblocking and can have lower latency under contention but is complex to get correct and can cause livelocks or high CPU usage due to spinning. Choose based on contention characteristics and maintainability; prefer hybrid approaches (e.g., sharded queues).

---

25) Question: How do you ensure NUMA‑aware allocation and thread placement?

Answer: Pin threads to cores (sched_setaffinity), allocate memory on the same NUMA node (numactl or libnuma), and use first‑touch initialization to ensure pages are allocated on the right node. Measure cross‑node memory access latency and tune thread‑pool partitioning and data placement to minimize remote accesses.

---

26) Question: Explain zero‑copy techniques and endianness concerns in network code.

Answer: Zero‑copy avoids copying payloads by passing pointers to buffers (e.g., mmap, scatter/gather I/O, or shared memory). For networking, you often parse in place using pointers and offsets; be cautious with endianness — use ntohl/htons or explicit byte reads for packed fields. Validate alignment before casting and prefer memcpy for unaligned fields.

---

27) Question: How do you handle backpressure in producer/consumer systems?

Answer: Use bounded queues with clear failure strategies (drop oldest/newest, block producer, or apply explicit flow control). Expose queue occupancy metrics and implement adaptive batching or rate limiting. For low‑latency, blocking producers may be undesirable; instead use cooperative techniques to shed load gracefully.

---

28) Question: Explain how you measure and prevent GC or other pauses from affecting latency.

Answer: Avoid GC by using non‑GC languages (C++), but third‑party libs or language runtimes may introduce pauses. For C++ ensure no hidden allocations or containers that grow without bounds; use memory pools, and preallocate. If using languages with GC, tune pause times, use incremental collectors, or isolate latency‑sensitive subsystems.

---

29) Question: How to implement determinism for matching engines across different servers?

Answer: Determinism requires same inputs processed in the same order. Avoid sources of non‑determinism: unordered maps, unseeded RNG, thread scheduling. Use single‑threaded core for matching or deterministic event ordering, and ensure identical floating‑point behavior (or use integer arithmetic) and consistent compiler flags across builds.

---

30) Question: What are page faults and how do they affect latency? How to mitigate?

Answer: Page faults occur when a page is not resident; handling causes kernel involvement and latency spikes. Mitigate by locking memory (mlock), using hugepages to reduce TLB misses, prefaulting memory (touch pages at startup), and controlling overcommit. Monitor /proc/vmstat and consider working set size relative to RAM.

---

31) Question: Discuss linkers, symbol visibility, and how to keep build artifacts small and fast to start.

Answer: Control symbol visibility using visibility attributes to reduce exported symbols and improve load/link time. Use LTO and strip unused code for release builds but keep debug symbols in separate files for postmortem. Use static linking to reduce dependency issues if acceptable, or carefully manage shared library versioning and SONAMEs.

---

32) Question: How do you use PGO and LTO to optimize performance safely?

Answer: Profile Guided Optimization (PGO) collects runtime profiles to inform optimizations (inlining, branch weights). LTO performs whole‑program optimizations. Use representative workloads for PGO; be cautious because PGO/LTO can change binary layout and timing — validate benchmarks and ensure reproducibility in CI.

---

33) Question: Explain lock-free memory reclamation strategies (hazard pointers, epoch, RCU).

Answer: Reclaiming memory safely in lock‑free code is hard because other threads might hold pointers. Hazard pointers let threads announce references to nodes; reclamation is delayed until safe. Epoch (read‑copy‑update style) advances epochs and reclaims when no reader is in earlier epoch. RCU is fast for reads but requires careful update protocols. Discuss complexity and performance tradeoffs.

---

34) Question: How would you diagnose a subtle data race seen only in production intermittently?

Answer: Gather logs, core dumps, and instrumentation traces. Add precise assertions and lightweight tracing around suspected hotspots. Reproduce with stress tests and TSan; if TSan can't find it due to timing differences, try record/replay (rr) or introduce deterministic scheduling hooks. Use small test harnesses to isolate the code path.

---

35) Question: What are practical ways to reduce syscalls on hot path?

Answer: Batch I/O, use user‑space buffering, use sendmmsg/recvmmsg for networking, avoid per‑message opens/closes, use preallocated resources, and do async or polled I/O. For timers, avoid frequent gettimeofday by caching timestamps or using TSC with calibration.

---

36) Question: Explain when to use fixed‑point arithmetic vs floating point in finance.

Answer: Use fixed‑point (integers scaled by factor) for monetary values requiring exact cents and reproducibility. Floating point is fine for analytics where small rounding is acceptable. Explain how fixed point removes rounding ambiguity and makes comparisons and serialization deterministic across platforms.

---

37) Question: How do you design for observability in low‑latency systems?

Answer: Expose metrics with low overhead (lock‑free counters, sampling), collect histograms for latency, provide tracing with minimal impact (sampling, async flushing), and ensure meaningful logs at appropriate levels. Avoid heavy logging on hot path; instead emit aggregated counters and use ring buffers for postmortem logging.

---

38) Question: Describe SIMD/vectorization use and when it's helpful.

Answer: SIMD speeds up data‑parallel operations (parsing, checksum, vector math). Use compiler intrinsics or auto‑vectorization; ensure data is aligned and memory layout is friendly. Profiling determines hotspots suitable for SIMD. Beware of branchy logic that inhibits vectorization and test across CPU microarchitectures.

---

39) Question: Explain techniques for safe hot code reload in trading systems.

Answer: Use versioned shared libraries with careful state migration, quiesce producers/consumers, or roll new processes behind a load balancer and switch traffic. Avoid in‑place patching; instead design components to be restartable and decouple state so it can be checkpointed and restored. Ensure backward compatibility of message formats.

---

40) Question: How to reason about and mitigate undefined behavior (UB) in C++?

Answer: UB is dangerous: compilers can optimize assuming it doesn't occur. Use sanitizers (ASan, UBSan), static analysis, and compile with warnings and -fno‑strict‑aliasing when necessary. Write defensive code, add assertions, and prefer well‑specified constructs. For library boundaries, document invariants and add runtime checks; when porting, test across compilers and architectures.

---

Notes and study tips
- Practice implementing lock‑free structures and measuring them under real concurrency. Use smaller reproductions and sanitizers to find bugs.
- Learn performance tooling: perf, FlameGraph, VTune, and how to interpret hardware counters.
- Study networking stacks (sockets, DPDK), OS tuning, and NUMA to design low‑latency systems.
- Prepare clear tradeoffs and system design answers: hedge‑fund interviews emphasize engineering tradeoffs, correctness under concurrency, and practical debugging.

If you want, I can:
- Produce individual whiteboard‑style solutions for any of these questions (code + explanation).
- Convert 8–12 of these into live coding exercises with test cases.
- Generate a printable PDF or a shorter crib sheet for rapid revision.
