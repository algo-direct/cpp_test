#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include "spsc_queue.h"
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <atomic>

int main()
{
    const size_t count = 1'000'000 * 500;
    SPSCQueue<uint64_t> q(1024);

    // start barrier to ensure we set affinity before the threads begin heavy work
    std::atomic<bool> start{false};

    std::thread producer([&]{
        // wait for main to finish pinning
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (uint64_t i = 1; i <= count; ++i) {
            while (!q.enqueue(i)) {
                // busy-wait
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&]{
        // wait for main to finish pinning
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        uint64_t expected = 1;
        uint64_t v;
        while (expected <= count) {
            if (q.dequeue(v)) {
                if (v != expected) {
                    std::cerr << "Mismatch: got " << v << " expected " << expected << '\n';
                    std::exit(2);
                }
                expected++;
            } else {
                std::this_thread::yield();
            }
        }
    });

    // Determine CPU cores to use
    unsigned int ncores = std::thread::hardware_concurrency();
    if (ncores == 0) {
        long conf = sysconf(_SC_NPROCESSORS_ONLN);
        if (conf > 0) ncores = static_cast<unsigned int>(conf);
    }

    auto pin_thread_to_cpu = [&](std::thread &t, int cpu)->bool{
        if (cpu < 0) return false;
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu, &cpuset);
        int rc = pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset);
        if (rc != 0) {
            std::cerr << "Warning: pthread_setaffinity_np failed for cpu " << cpu << " (rc=" << rc << ")\n";
            return false;
        }
        return true;
    };

    int prod_cpu = 3;
    int cons_cpu = 5;

    if (ncores <= 6) {
        std::cerr << "Warning: only " << ncores << " CPU available; producer and consumer will run on same core.\n";
    }
    else {
        std::cout << "Pinning producer to CPU " << prod_cpu << " and consumer to CPU " << cons_cpu << "\n";
    }

    // Pin threads before starting the workload
    pin_thread_to_cpu(producer, prod_cpu);
    pin_thread_to_cpu(consumer, cons_cpu);

    // start timing and release threads
    auto start_time = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);

    producer.join();
    consumer.join();
    auto end = std::chrono::steady_clock::now();

    double secs = std::chrono::duration<double>(end - start_time).count();
    std::cout << "Transferred " << count << " items in " << secs << " seconds (" << (count / secs) << " ops/s)\n";
    return 0;
}
