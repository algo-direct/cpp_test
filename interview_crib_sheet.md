# C++ / Low‑Latency Interview Crib Sheet — One Page

Keep this as a fast refresher before an interview. Short reminders, key commands, and mental checklist.

Core C++ (language & idioms)
- RAII: acquire in ctor, release in dtor. Use `std::unique_ptr`, RAII wrappers for FILE*, sockets.
- Rule of Five: define move/copy/dtor when you own resources; prefer move semantics and mark noexcept when possible.
- Smart pointers: `unique_ptr` for ownership, `shared_ptr` when shared ownership necessary — avoid for hot paths.
- constexpr & consteval: use for compile-time computation; virtual `constexpr` OK if devirtualizable at compile time.
- Undefined Behavior: avoid signed overflow, out‑of‑bounds, use sanitizers (ASan/UBSan/TSan).

Concurrency & Lock‑Free
- Atomics: memory_order_relaxed (counters), release/acquire (producer/consumer), seq_cst when unsure.
- CAS loop pattern:
  - use compare_exchange_weak in retry loops; handle spurious failures.
- False sharing: align hot fields (alignas(64)), pad per‑thread data.
- Common patterns: SPSC ring buffer (no CAS), Vyukov MPMC (per-slot seq + CAS), hazard pointers / epoch for reclamation.
- Backoff: progressive spin (pause -> yield -> sleep(0/1)). Measure not guess.

Performance & OS tuning
- Profile first: perf, flamegraphs, VTune. Measure P50/P95/P99/P99.9 not only mean.
- CPU tuning: pin threads (sched_setaffinity), isolate cores, disable C‑states on microbenchmark hosts.
- NUMA: allocate and first‑touch on correct node, pin threads and memory together.
- Memory: hugepages, mlock to avoid page faults, prewarm allocations.

Networking & I/O
- Zero copy when possible; parse in place, use scatter/gather (sendmmsg/recvmmsg).
- Endianness: use ntohl/htons or explicit decode; don't reinterpret_cast unaligned network data.
- Kernel bypass: DPDK for ultra‑low latency; cost/complexity tradeoffs.

Data structures & algorithms
- Order book: price levels + per‑price order list. Choose structure by requirements (balanced tree vs bucketed array).
- Timer: hierarchical timer wheels for many timers; heap for moderate counts.
- Branchless code: use conditional moves, lookup tables for hotspots vulnerable to mispredictions.

Debugging & Testing
- Reproduce: collect core + exact binary + symbols; use gdb and read maps at crash time.
- Concurrency: use TSan/helgrind; for deterministic tests add scheduler hooks or record/replay (rr).
- Memory issues: ASan; use smaller reproductions and unit tests.

Build & Deploy
- Reproducible builds: pin toolchain, store build artifacts, separate debug symbols.
- CMake: set CMAKE_CXX_STANDARD and CMAKE_CXX_EXTENSIONS as needed; use target_compile_options for fine control.
- PGO/LTO: use with representative workloads; validate performance impact.

Practical interview checklist (quick answers and examples)
- Explain RAII + show `unique_ptr<int[]> p(new int[n]);`.
- Demonstrate move ctor: `Foo(Foo&&) noexcept = default;` and when to implement custom.
- Write a simple SPSC ring buffer sketch and explain why no CAS is needed.
- Describe acquire/release pair for a flag protecting data visibility.
- Describe false sharing and show `struct alignas(64) Padded { uint64_t x; char pad[56]; }`.

Common commands & flags
- Build/compile (CMake):
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build --target mpmc_demo -j
- Sanitizers:
  g++ -fsanitize=address,undefined -g -O1 test.cpp
  TSAN: -fsanitize=thread (use separate run to avoid ASan+TSan mix)
- Profiling:
  perf record -F 99 -g -- ./build/mpmc_demo
  perf report --call-graph=dwarf

Final tips
- Always explain tradeoffs: correctness vs latency, maintainability vs micro‑optimizations.
- When presenting a design, quantify: estimate latencies, costs, and failure modes.
- Practice whiteboard implementations for lock‑free queues, parsers, and order‑book operations.

Want this as a printable PDF or a one‑page A4 layout? I can generate a PDF or a compact Markdown-to-PDF render.
