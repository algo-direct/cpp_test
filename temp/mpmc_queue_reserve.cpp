// Reservation-based ring MPMC queue with separate reserved and committed head/tail.
// Producers reserve a ticket (uncommitted tail), wait for the slot to be free,
// write data, then publish the ticket by setting the slot's seq to the ticket.
// A background advance of tail_commit (performed by producers) makes ranges available
// to consumers. Consumers reserve a ticket (uncommitted head), wait until the slot
// contains their ticket (committed), consume the data, then mark the slot free.

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>
#include <limits>

template<typename T>
class MPMCReserveQueue {
public:
    explicit MPMCReserveQueue(size_t capacity)
    : cap_(round_pow2(std::max<size_t>(2, capacity))), mask_(cap_ - 1), buffer_(cap_) {
        for (size_t i = 0; i < cap_; ++i) buffer_[i].seq.store(EMPTY, std::memory_order_relaxed);
        tail_reserve_.store(0, std::memory_order_relaxed);
        tail_commit_.store(0, std::memory_order_relaxed);
        head_reserve_.store(0, std::memory_order_relaxed);
        head_commit_.store(0, std::memory_order_relaxed);
    }

    // Non-copyable
    MPMCReserveQueue(const MPMCReserveQueue&) = delete;
    MPMCReserveQueue& operator=(const MPMCReserveQueue&) = delete;

    bool try_push(const T& v) {
        // Reserve a ticket but ensure we don't overrun capacity (respect head_commit)
        uint64_t ticket;
        backoff_t backoff;
        while (true) {
            uint64_t tail = tail_reserve_.load(std::memory_order_relaxed);
            uint64_t headc = head_commit_.load(std::memory_order_acquire);
            if (tail - headc >= cap_) {
                // queue full, wait a bit
                backoff.spin();
                continue;
            }
            if (tail_reserve_.compare_exchange_weak(tail, tail + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                ticket = tail;
                break;
            }
        }

        size_t idx = ticket & mask_;
        Cell& cell = buffer_[idx];

        // wait until cell is free (seq == EMPTY)
        while (cell.seq.load(std::memory_order_acquire) != EMPTY) {
            backoff.spin();
        }

        // write payload
        cell.value = v;

    // publish ticket
    cell.seq.store(ticket, std::memory_order_release);
    if (ticket < 8) std::cout << "producer published ticket=" << ticket << " idx=" << idx << "\n";

        // try to advance tail_commit: any producer (or consumer) may help
        advance_tail_commit();
        return true;
    }

    bool try_pop(T& out) {
        // Reserve only when there is at least one committed element (tail_commit_ > head_reserve_)
        uint64_t ticket;
        backoff_t backoff;
        while (true) {
            uint64_t headr = head_reserve_.load(std::memory_order_relaxed);
            // Use tail_reserve_ (producers' reservations) to avoid over-reserving when producers
            // have not yet advanced tail_commit_. This prevents consumers from grabbing tickets
            // that are beyond what producers have reserved.
            uint64_t tailr = tail_reserve_.load(std::memory_order_acquire);
            if (headr >= tailr) {
                // no committed items yet
                backoff.spin();
                continue;
            }
            if (head_reserve_.compare_exchange_weak(headr, headr + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                ticket = headr;
                break;
            }
        }

        size_t idx = ticket & mask_;
        Cell& cell = buffer_[idx];

        // wait until producer published this ticket (should normally be immediate because tail_commit_ > ticket)
        if (ticket < 8) std::cout << "consumer waiting for ticket=" << ticket << " idx=" << idx << "\n";
        while (cell.seq.load(std::memory_order_acquire) != ticket) {
            backoff.spin();
        }

        // consume
        out = cell.value;

        // mark free
        cell.seq.store(EMPTY, std::memory_order_release);

        // try to advance head_commit (help free consecutive slots)
        advance_head_commit();
        return true;
    }

    size_t capacity() const { return cap_; }

    uint64_t tail_committed() const { return tail_commit_.load(std::memory_order_acquire); }
    uint64_t head_committed() const { return head_commit_.load(std::memory_order_acquire); }
    // Debug accessors (for tests)
    uint64_t debug_tail_reserve() const { return tail_reserve_.load(std::memory_order_acquire); }
    uint64_t debug_head_reserve() const { return head_reserve_.load(std::memory_order_acquire); }
    uint64_t debug_cell_seq(size_t idx) const { return buffer_[idx & mask_].seq.load(std::memory_order_acquire); }

private:
    struct Cell {
        std::atomic<uint64_t> seq; // EMPTY or ticket value
        T value;
    };

    static inline uint64_t const EMPTY = std::numeric_limits<uint64_t>::max();

    struct backoff_t {
        int spins = 0;
        void spin() {
            if (spins < 10) {
                ++spins;
                asm volatile("pause" ::: "memory");
            } else if (spins < 20) {
                ++spins; std::this_thread::yield();
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        }
    };

    void advance_tail_commit() {
        uint64_t cur = tail_commit_.load(std::memory_order_acquire);
        while (true) {
            size_t idx = cur & mask_;
            uint64_t seq = buffer_[idx].seq.load(std::memory_order_acquire);
            if (seq == cur) {
                // can advance
                if (tail_commit_.compare_exchange_weak(cur, cur + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    if (cur < 8) std::cout << "advance_tail_commit: advanced to " << (cur+1) << "\n";
                    // advanced, continue to try next
                    continue;
                } else {
                    // cur updated by another thread, retry
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
            if (seq == EMPTY) {
                if (head_commit_.compare_exchange_weak(cur, cur + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    continue;
                } else continue;
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

// Simple test similar to other temp tests
int main() {
    const int producers = 4;
    const int consumers = 4;
    const int per_producer = 10000; // reduced for debug
    const int total = producers * per_producer;

    MPMCReserveQueue<int> q(1024);
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
                q.try_push(v);
                if (v < 20) std::cout << "P" << p << " pushed " << v << "\n";
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
                    if (v < 20) std::cout << "C popped " << v << "\n";
                    consumed.fetch_add(1, std::memory_order_relaxed);
                    local_sum += v;
                } else {
                    std::this_thread::yield();
                }
            }
            sum_cons.fetch_add(local_sum, std::memory_order_relaxed);
        });
    }

    // Start a watchdog thread to detect stalls and dump diagnostics
    std::atomic<int> last_progress{0};
    std::thread watchdog([&]() {
        int last = produced.load();
        while (consumed.load() < total) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            int cur = produced.load();
            if (cur == last) {
                std::cerr << "WATCHDOG: no new produced items, dumping diagnostics\n";
                std::cerr << "tail_reserve=" << q.debug_tail_reserve() << " tail_commit=" << q.tail_committed() << " head_reserve=" << q.debug_head_reserve() << " head_commit=" << q.head_committed() << "\n";
                // sample a few cells around head_commit
                uint64_t h = q.head_committed();
                for (uint64_t i = 0; i < 8; ++i) {
                    size_t idx = (h + i) & (q.capacity()-1);
                    std::cerr << "cell[" << idx << "] seq=" << q.debug_cell_seq(idx) << "\n";
                }
                abort();
            }
            last = cur;
        }
    });

    for (auto &t : pts) t.join();
    for (auto &t : cts) t.join();
    watchdog.join();

    if (produced.load() != total) { std::cerr << "produced mismatch\n"; return 2; }
    if (consumed.load() != total) { std::cerr << "consumed mismatch\n"; return 3; }
    if (sum_prod.load() != sum_cons.load()) { std::cerr << "sum mismatch: " << sum_prod.load() << " != " << sum_cons.load() << "\n"; return 4; }

    std::cout << "mpmc_queue_reserve: PASS items=" << total << " sum=" << sum_cons.load() << "\n";
    return 0;
}
