#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

#include "mpmc_queue_ms.h"
#include <mutex>

int main(int argc, char** argv) {
    const int producers = 4;
    const int consumers = 4;
    const int per_producer = 50000;
    const int total = producers * per_producer;

    MPMCQueue<int> q;
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<long long> sum_produced{0};
    std::atomic<long long> sum_consumed{0};

    // Producers
    std::vector<std::thread> pts;
    for (int p = 0; p < producers; ++p) {
        pts.emplace_back([&, p]() {
            int base = p * per_producer;
            for (int i = 1; i <= per_producer; ++i) {
                int v = base + i;
                q.push(v);
                sum_produced.fetch_add(v, std::memory_order_relaxed);
                produced.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Consumers
    std::vector<std::thread> cts;
    std::vector<char> seen(total + 1, 0);
    std::mutex seen_mtx;
    for (int c = 0; c < consumers; ++c) {
        cts.emplace_back([&]() {
            int local_count = 0;
            long long local_sum = 0;
            while (consumed.load(std::memory_order_acquire) < total) {
                int v;
                if (q.try_pop(v)) {
                    ++local_count;
                    local_sum += v;
                    consumed.fetch_add(1, std::memory_order_relaxed);
                    // record and detect duplicates/missing
                    if (v >= 1 && v <= total) {
                        std::lock_guard<std::mutex> lk(seen_mtx);
                        if (seen[v]) {
                            std::cerr << "Duplicate value observed: " << v << "\n";
                        }
                        seen[v] = 1;
                    } else {
                        std::cerr << "Out-of-range value: " << v << "\n";
                    }
                } else {
                    std::this_thread::yield();
                }
            }
            sum_consumed.fetch_add(local_sum, std::memory_order_relaxed);
        });
    }

    for (auto &t : pts) t.join();
    for (auto &t : cts) t.join();

    long long prod = sum_produced.load();
    long long cons = sum_consumed.load();
    if (produced != total) {
        std::cerr << "produced count mismatch: " << produced.load() << " != " << total << "\n";
        return 2;
    }
    if (consumed != total) {
        std::cerr << "consumed count mismatch: " << consumed.load() << " != " << total << "\n";
        return 3;
    }
    if (prod != cons) {
        std::cerr << "sum mismatch: " << prod << " != " << cons << "\n";
        return 4;
    }

    std::cout << "mpmc_stress: PASS (items=" << total << ", sum=" << prod << ")\n";
    return 0;
}
