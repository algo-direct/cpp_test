#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include "mpmc_queue.h"

int main()
{
    // configurable via env vars: MPMC_PRODUCERS, MPMC_CONSUMERS, MPMC_PER_PROD
    auto getenv_u = [](const char* name, uint64_t fallback)->uint64_t{
        const char* v = std::getenv(name);
        if (!v) return fallback;
        try { return std::stoull(v); } catch(...) { return fallback; }
    };
    const unsigned int producers = static_cast<unsigned int>(getenv_u("MPMC_PRODUCERS", 4));
    const unsigned int consumers = static_cast<unsigned int>(getenv_u("MPMC_CONSUMERS", 3));
    const uint64_t per_producer = getenv_u("MPMC_PER_PROD", 2000000ULL); // tune as needed
    const uint64_t total = per_producer * producers;

    MPMCQueue<uint64_t> q(1024);

    std::atomic<uint64_t> produced_sum{0};
    std::atomic<uint64_t> consumed_sum{0};

    // Launch producers
    std::vector<std::thread> pth;
    for (unsigned int p = 0; p < producers; ++p) {
        pth.emplace_back([p, per_producer, &q, &produced_sum]{
            uint64_t base = uint64_t(p) * per_producer;
            uint64_t local_sum = 0;
            for (uint64_t i = 0; i < per_producer; ++i) {
                uint64_t v = base + i + 1;
                q.enqueue(v);
                local_sum += v;
            }
            produced_sum.fetch_add(local_sum, std::memory_order_relaxed);
        });
    }

    std::atomic<uint64_t> consumed_count{0};
    // Launch consumers
    std::vector<std::thread> cth;
    for (unsigned int c = 0; c < consumers; ++c) {
        cth.emplace_back([&]{
            uint64_t local_sum = 0;
            while (true) {
                uint64_t v;
                if (q.try_dequeue(v)) {
                    local_sum += v;
                    uint64_t prev = consumed_count.fetch_add(1, std::memory_order_relaxed);
                    if (prev + 1 >= total) break;
                } else {
                    // If enough items have been consumed by other threads, exit.
                    if (consumed_count.load(std::memory_order_relaxed) >= total) break;
                    // try again briefly
                    std::this_thread::yield();
                }
            }
            consumed_sum.fetch_add(local_sum, std::memory_order_relaxed);
        });
    }

    auto start = std::chrono::steady_clock::now();
    for (auto &t : pth) t.join();
    for (auto &t : cth) t.join();
    auto end = std::chrono::steady_clock::now();

    double secs = std::chrono::duration<double>(end - start).count();
    uint64_t prod = produced_sum.load(std::memory_order_relaxed);
    uint64_t cons = consumed_sum.load(std::memory_order_relaxed);
    std::cout << "Produced sum=" << prod << " Consumed sum=" << cons << "\n";
    if (prod != cons) {
        std::cerr << "Sum mismatch!" << std::endl;
        return 2;
    }

    std::cout << "Transferred " << total << " items in " << secs << " seconds (" << (total / secs) << " ops/s)\n";
    return 0;
}
