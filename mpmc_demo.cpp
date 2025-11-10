#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <sstream>
#include <iomanip>
#include "mpmc_queue.h"

int main(int argc, char** argv)
{
    // Parse simple command-line args (defaults match previous env defaults)
    unsigned int producers = 4;
    unsigned int consumers = 3;
    uint64_t per_producer = 2000000ULL;
    bool backoff = true; // when true consumers sleep briefly when empty
    uint64_t backoff_us = 50;

    for (int i = 1; i < argc; ++i) {
        std::string s(argv[i]);
        if ((s == "-p") || (s == "--producers")) {
            if (i + 1 < argc) producers = static_cast<unsigned int>(std::stoul(argv[++i]));
        } else if ((s == "-c") || (s == "--consumers")) {
            if (i + 1 < argc) consumers = static_cast<unsigned int>(std::stoul(argv[++i]));
        } else if ((s == "-n") || (s == "--per-producer")) {
            if (i + 1 < argc) per_producer = std::stoull(argv[++i]);
        } else if (s == "--no-backoff") {
            backoff = false;
        } else if (s == "--backoff-us") {
            if (i + 1 < argc) backoff_us = std::stoull(argv[++i]);
        } else if ((s == "-h") || (s == "--help")) {
            std::cout << "Usage: " << argv[0] << " [--producers N] [--consumers N] [--per-producer N] [--no-backoff] [--backoff-us N]\n";
            return 0;
        }
    }

    const uint64_t total = per_producer * producers;

    MPMCQueue<uint64_t> q{};

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
    std::atomic<bool> producers_done{false};
    // Launch consumers
    std::vector<std::thread> cth;
    for (unsigned int c = 0; c < consumers; ++c) {
        cth.emplace_back([&, c]{
            uint64_t local_sum = 0;
            uint64_t spin = 0;
            while (true) {
                uint64_t v;
                if (q.try_dequeue(v)) {
                    local_sum += v;
                    uint64_t prev = consumed_count.fetch_add(1, std::memory_order_relaxed);
                    if (prev + 1 >= total) break;
                    spin = 0;
                } else {
                    // If enough items have been consumed by other threads, exit.
                    if (consumed_count.load(std::memory_order_relaxed) >= total) break;
                    // If producers are done and queue empty, exit
                    if (producers_done.load(std::memory_order_relaxed)) {
                        if (consumed_count.load(std::memory_order_relaxed) >= total) break;
                    }
                    // Backoff strategy: yield for a while, then sleep if enabled
                    if (spin < 50) {
                        ++spin;
                        std::this_thread::yield();
                    } else if (backoff) {
                        std::this_thread::sleep_for(std::chrono::microseconds(backoff_us));
                    } else {
                        std::this_thread::yield();
                    }
                }
            }
            consumed_sum.fetch_add(local_sum, std::memory_order_relaxed);
        });
    }

    auto start = std::chrono::steady_clock::now();
    for (auto &t : pth) t.join();
    producers_done.store(true, std::memory_order_relaxed);
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

    // Print queue instrumentation (lightweight counters)
    std::cout << "Queue stats: spins=" << q.stats_spins() << " cas_failures=" << q.stats_cas_failures() << "\n";

    // human-readable ops/sec formatter
    auto human_rate = [](double v) {
        const char* suf[] = {"", "K", "M", "G", "T"};
        size_t idx = 0;
        while (v >= 1000.0 && idx < 4) { v /= 1000.0; ++idx; }
        std::ostringstream os;
        os.setf(std::ios::fixed);
        if (v >= 100.0) os << std::setprecision(0);
        else if (v >= 10.0) os << std::setprecision(1);
        else os << std::setprecision(2);
        os << v << suf[idx] << " ops/s";
        return os.str();
    };

    double rate = (secs > 0.0) ? (static_cast<double>(total) / secs) : 0.0;
    std::cout << "Transferred " << total << " items in " << secs << " seconds (" << human_rate(rate) << ")\n";
    return 0;
}
