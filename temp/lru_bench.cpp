// Benchmark comparing Sharded LRU (mutex-based) vs two lock-free variants.
// This includes the implementations by including the .cpp files with LRU_BENCH
// defined to prevent their standalone mains.

#define LRU_BENCH
#include "lru_cache.cpp"
#include "lru_cache_lockfree.cpp"

#include <atomic>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

using namespace std::chrono;

template<typename Cache>
double run_workload(Cache &cache, int threads, int duration_s, int key_space) {
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> ops{0};
    auto worker = [&](int id){
        std::mt19937_64 rng(id + 12345);
        std::uniform_int_distribution<int> dist(1, key_space);
        while (!stop.load(std::memory_order_acquire)) {
            int k = dist(rng);
            cache.put(k, k);
            auto v = cache.get(k);
            (void)v;
            ops.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> ts;
    for (int i = 0; i < threads; ++i) ts.emplace_back(worker, i);
    std::this_thread::sleep_for(std::chrono::seconds(duration_s));
    stop.store(true, std::memory_order_release);
    for (auto &t : ts) t.join();
    return double(ops.load()) / duration_s;
}

int main() {
    const int threads = 8;
    const int duration_s = 3; // run each test 3s
    const int key_space = 10000;

    std::cout << "Benchmark: " << threads << " threads, " << duration_s << "s each, key space=" << key_space << "\n";

    // Sharded LRU
    ShardedLRUCache<int,int> sharded(16384, 8);
    double rate_sharded = run_workload(sharded, threads, duration_s, key_space);
    std::cout << "Sharded LRU throughput: " << rate_sharded << " ops/s\n";

    // Lock-free Hazard Pointers
    LockFreeLRU_HazardPointers<int,int> lf_hp(128, 16384);
    double rate_lf_hp = run_workload(lf_hp, threads, duration_s, key_space);
    std::cout << "LockFree (hazard ptrs) throughput: " << rate_lf_hp << " ops/s\n";

    // Lock-free per-node CAS (shared_ptr)
    LockFreeLRU_PerNodeCAS<int,int> lf_cas(128, 16384);
    double rate_lf_cas = run_workload(lf_cas, threads, duration_s, key_space);
    std::cout << "LockFree (shared_ptr CAS) throughput: " << rate_lf_cas << " ops/s\n";

    return 0;
}
