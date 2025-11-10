// Ticket-based ring MPMC queue with reserve (ticket) and commit counters.
// This is a generation-based ring: per-slot seq initializes to index; producers wait for seq==ticket,
// publish by setting seq = ticket+1; consumers wait for seq==ticket+1 and after consume set seq = ticket + cap.
// We expose tail_reserve (tickets handed out) and tail_commit (tickets published), and similarly head_reserve/head_commit.

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

template<typename T>
class MPMCTicketQueue {
public:
    explicit MPMCTicketQueue(size_t capacity)
    : cap_(round_pow2(std::max<size_t>(2, capacity))), mask_(cap_ - 1), buffer_(cap_) {
        for (size_t i = 0; i < cap_; ++i) buffer_[i].seq.store(i, std::memory_order_relaxed);
        tail_reserve_.store(0, std::memory_order_relaxed);
        tail_commit_.store(0, std::memory_order_relaxed);
        head_reserve_.store(0, std::memory_order_relaxed);
        head_commit_.store(0, std::memory_order_relaxed);
    }

    MPMCTicketQueue(const MPMCTicketQueue&) = delete;
    MPMCTicketQueue& operator=(const MPMCTicketQueue&) = delete;

    void push(const T& v) {
        // Blocking push implemented on top of try_push
        while (!try_push(v)) {
            // queue full: back off briefly
            std::this_thread::yield();
        }
    }

    bool try_push(const T& v) {
        // Reserve a ticket only if there's space (tail_reserve - head_commit < cap_)
        uint64_t ticket;
        while (true) {
            ticket = tail_reserve_.load(std::memory_order_relaxed);
            uint64_t head_c = head_commit_.load(std::memory_order_acquire);
            if (ticket - head_c >= cap_) return false; // full
            if (tail_reserve_.compare_exchange_weak(ticket, ticket + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) break;
            // otherwise retry
        }

        size_t idx = ticket & mask_;
        Cell& c = buffer_[idx];

        backoff_t backoff;
        while (c.seq.load(std::memory_order_acquire) != ticket) {
            backoff.spin();
        }

        c.value = v;
        // publish
        c.seq.store(ticket + 1, std::memory_order_release);

        // try advance tail_commit
        advance_tail_commit();
        return true;
    }

    bool try_pop(T& out) {
        // Reserve a ticket only if there is a published item (tail_commit > head_reserve)
        uint64_t ticket;
        while (true) {
            ticket = head_reserve_.load(std::memory_order_relaxed);
            uint64_t tail_c = tail_commit_.load(std::memory_order_acquire);
            if (ticket >= tail_c) return false; // no published items available
            if (head_reserve_.compare_exchange_weak(ticket, ticket + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) break;
            // otherwise retry with updated 'ticket'
        }

        size_t idx = ticket & mask_;
        Cell& c = buffer_[idx];

        backoff_t backoff;
        while (c.seq.load(std::memory_order_acquire) != ticket + 1) {
            backoff.spin();
        }

        out = c.value;
        // free for next round: set seq = ticket + cap_
        c.seq.store(ticket + cap_, std::memory_order_release);

        // try advance head_commit
        advance_head_commit();
        return true;
    }

    // Debug accessors
    uint64_t tail_reserve() const { return tail_reserve_.load(std::memory_order_acquire); }
    uint64_t tail_commit() const { return tail_commit_.load(std::memory_order_acquire); }
    uint64_t head_reserve() const { return head_reserve_.load(std::memory_order_acquire); }
    uint64_t head_commit() const { return head_commit_.load(std::memory_order_acquire); }

    size_t capacity() const { return cap_; }
    uint64_t seq_at(size_t idx) const { return buffer_[idx].seq.load(std::memory_order_relaxed); }

private:
    struct Cell {
        std::atomic<uint64_t> seq;
        T value;
    };

    struct backoff_t {
        int spins = 0;
        void spin() {
            if (spins < 6) { ++spins; asm volatile("pause" ::: "memory"); }
            else if (spins < 20) { ++spins; std::this_thread::yield(); }
            else std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    };

    void advance_tail_commit() {
        uint64_t cur = tail_commit_.load(std::memory_order_acquire);
        while (true) {
            size_t idx = cur & mask_;
            uint64_t seq = buffer_[idx].seq.load(std::memory_order_acquire);
            if (seq == cur + 1) {
                if (tail_commit_.compare_exchange_weak(cur, cur + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    // advanced successfully; update local cursor to the new value and continue
                    cur = cur + 1;
                    continue;
                } else {
                    // cur was updated with current tail_commit value by compare_exchange_weak; retry
                    continue;
                }
            }
            break;
        }
    }

    void advance_head_commit() {
        uint64_t cur = head_commit_.load(std::memory_order_acquire);
        while (true) {
            size_t idx = cur & mask_;
            uint64_t seq = buffer_[idx].seq.load(std::memory_order_acquire);
            if (seq >= cur + cap_) { // freed for next rounds
                if (head_commit_.compare_exchange_weak(cur, cur + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    cur = cur + 1;
                    continue;
                } else {
                    continue;
                }
            }
            break;
        }
    }

    static size_t round_pow2(size_t v) {
        v--; v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
        if (sizeof(size_t) > 4) v |= v >> 32;
        return v + 1;
    }

    const size_t cap_;
    const size_t mask_;
    std::vector<Cell> buffer_;

    std::atomic<uint64_t> tail_reserve_;
    std::atomic<uint64_t> tail_commit_;
    std::atomic<uint64_t> head_reserve_;
    std::atomic<uint64_t> head_commit_;
};

// Test
int main() {
    const int producers = 4;
    const int consumers = 4;
    const int per_producer = 10000;
    const int total = producers * per_producer;

    MPMCTicketQueue<int> q(1024);
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<long long> sum_prod{0};
    std::atomic<long long> sum_cons{0};

    std::vector<std::thread> pts;
    for (int p = 0; p < producers; ++p) {
        pts.emplace_back([&, p]() {
            int base = p * per_producer;
            for (int i = 1; i <= per_producer; ++i) {
                int v = base + i;
                q.push(v);
                produced.fetch_add(1, std::memory_order_relaxed);
                sum_prod.fetch_add(v, std::memory_order_relaxed);
            }
        });
    }

    std::vector<std::thread> cts;
    for (int c = 0; c < consumers; ++c) {
        cts.emplace_back([&]() {
            long long local_sum = 0;
            while (consumed.load(std::memory_order_acquire) < total) {
                int v;
                if (q.try_pop(v)) {
                    consumed.fetch_add(1, std::memory_order_relaxed);
                    local_sum += v;
                } else {
                    std::this_thread::yield();
                }
            }
            sum_cons.fetch_add(local_sum, std::memory_order_relaxed);
        });
    }

    // No watchdog in benchmark mode

    // benchmark: measure elapsed time
    auto start = std::chrono::steady_clock::now();
    for (auto &t : pts) t.join();
    for (auto &t : cts) t.join();
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    if (produced.load() != total) { std::cerr << "produced mismatch\n"; return 2; }
    if (consumed.load() != total) { std::cerr << "consumed mismatch\n"; return 3; }
    if (sum_prod.load() != sum_cons.load()) { std::cerr << "sum mismatch: " << sum_prod.load() << " != " << sum_cons.load() << "\n"; return 4; }

    double secs = elapsed.count();
    double rate = total / secs;
    std::cout << "mpmc_ticket: PASS items=" << total << " sum=" << sum_cons.load() << " time=" << secs << "s rate=" << rate << " items/s\n";
    return 0;
}
