#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include "mpsc_queue.h"

int main()
{
    const unsigned int producers = 4;
    const uint64_t per_producer = 5'000'000; // adjust if needed
    const uint64_t total = per_producer * producers;

    MPSCQueue<uint64_t> q(1024);

    std::atomic<uint64_t> produced_sum{0};
    std::atomic<uint64_t> consumed_sum{0};

    // Launch producers
    std::vector<std::thread> ths;
    for (unsigned int p = 0; p < producers; ++p) {
        ths.emplace_back([p, per_producer, &q, &produced_sum]{
            uint64_t base = uint64_t(p) * per_producer;
            uint64_t local_sum = 0;
            for (uint64_t i = 0; i < per_producer; ++i) {
                uint64_t v = base + i + 1; // non-zero
                // blocking enqueue
                q.enqueue(v);
                local_sum += v;
            }
            produced_sum.fetch_add(local_sum, std::memory_order_relaxed);
        });
    }

    // Consumer
    std::thread consumer([&]{
        uint64_t got = 0;
        uint64_t local_sum = 0;
        while (got < total) {
            uint64_t v;
            q.dequeue(v);
            local_sum += v;
            ++got;
        }
        consumed_sum.store(local_sum, std::memory_order_relaxed);
    });

    auto start = std::chrono::steady_clock::now();

    for (auto &t : ths) t.join();
    consumer.join();

    auto end = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(end - start).count();

    uint64_t prod = produced_sum.load(std::memory_order_relaxed);
    uint64_t cons = consumed_sum.load(std::memory_order_relaxed);

    std::cout << "Producers produced sum=" << prod << " consumer consumed sum=" << cons << "\n";
    if (prod != cons) {
        std::cerr << "Sum mismatch!" << std::endl;
        return 2;
    }

    std::cout << "Transferred " << total << " items in " << secs << " seconds (" << (total / secs) << " ops/s)\n";
    return 0;
}
