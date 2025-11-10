// Michael & Scott lock-free MPMC queue (linked-list based)
// Note: This is a standard lock-free algorithm for multiple producers and consumers.
// Memory reclamation: nodes are deleted by the consumer that advances the head, which
// is safe in the M&S algorithm once head is moved.

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>

template<typename T>
class MPMCQueue {
    struct Node {
        std::atomic<Node*> next;
        T value;
        Node(T&& v) : next(nullptr), value(std::move(v)) {}
        Node() : next(nullptr), value() {}
    };

public:
    MPMCQueue() {
        Node* dummy = new Node();
        head_.store(dummy, std::memory_order_relaxed);
        tail_.store(dummy, std::memory_order_relaxed);
    }

    ~MPMCQueue() {
        // drain and delete nodes
        Node* n = head_.load(std::memory_order_relaxed);
        while (n) {
            Node* next = n->next.load(std::memory_order_relaxed);
            delete n;
            n = next;
        }
    }

    void push(const T& v) { push_impl(T(v)); }
    void push(T&& v) { push_impl(std::move(v)); }

    bool try_pop(T& out) {
        while (true) {
            Node* head = head_.load(std::memory_order_acquire);
            Node* tail = tail_.load(std::memory_order_acquire);
            Node* next = head->next.load(std::memory_order_acquire);
            if (head == tail) {
                if (next == nullptr) return false; // empty
                // tail falling behind, try to advance
                tail_.compare_exchange_weak(tail, next, std::memory_order_release, std::memory_order_relaxed);
            } else {
                // read value before CAS
                if (next == nullptr) continue;
                // Attempt to swing head to the next node
                if (head_.compare_exchange_weak(head, next, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    out = std::move(next->value);
                    delete head; // safe: no other thread will access old head after successful CAS
                    return true;
                }
            }
        }
    }

private:
    template<typename U>
    void push_impl(U&& v) {
        Node* node = new Node(std::forward<U>(v));
        while (true) {
            Node* tail = tail_.load(std::memory_order_acquire);
            Node* next = tail->next.load(std::memory_order_acquire);
            if (tail == tail_.load(std::memory_order_acquire)) {
                if (next == nullptr) {
                    // try to link new node at the end
                    if (tail->next.compare_exchange_weak(next, node, std::memory_order_release, std::memory_order_relaxed)) {
                        // try to swing tail to the inserted node
                        tail_.compare_exchange_weak(tail, node, std::memory_order_release, std::memory_order_relaxed);
                        return;
                    }
                } else {
                    // tail is behind, advance it
                    tail_.compare_exchange_weak(tail, next, std::memory_order_release, std::memory_order_relaxed);
                }
            }
        }
    }

    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
};

int main() {
    const int producers = 4;
    const int consumers = 4;
    const int per_producer = 50000; // items per producer
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
                } else {
                    std::this_thread::yield();
                }
            }
            sum_consumed.fetch_add(local_sum, std::memory_order_relaxed);
        });
    }

    for (auto &t : pts) t.join();
    for (auto &t : cts) t.join();

    // Validate
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

    std::cout << "mpmc_queue_ms: PASS (items=" << total << ", sum=" << prod << ")\n";
    return 0;
}
