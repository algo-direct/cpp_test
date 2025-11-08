#pragma once
#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>

// Bounded multi-producer multi-consumer queue (Vyukov MPMC bounded queue).
// - Capacity rounded up to power-of-two.
// - Multiple producers and multiple consumers supported.
// Uses per-slot sequence numbers and atomic head/tail with CAS on reservation.
template<typename T>
class MPMCQueue {
public:
    explicit MPMCQueue(size_t capacity)
    {
        cap_ = 1u;
        while (cap_ < capacity) cap_ <<= 1u;
        mask_ = cap_ - 1u;
        buffer_.reset(static_cast<Cell*>(operator new[](sizeof(Cell) * cap_)));
        for (size_t i = 0; i < cap_; ++i) {
            new (&buffer_[i]) Cell();
            buffer_[i].seq.store(i, std::memory_order_relaxed);
        }
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    ~MPMCQueue()
    {
        for (size_t i = 0; i < cap_; ++i) buffer_[i].~Cell();
        operator delete[](buffer_.release());
    }

    MPMCQueue(const MPMCQueue&) = delete;
    MPMCQueue& operator=(const MPMCQueue&) = delete;

    // Blocking enqueue: reserve a ticket then wait for the slot to become available.
    void enqueue(const T& item)
    {
        const size_t pos = tail_.fetch_add(1, std::memory_order_relaxed);
        Cell &cell = buffer_[pos & mask_];
        for (;;) {
            size_t seq = cell.seq.load(std::memory_order_acquire);
            long long dif = (long long)seq - (long long)pos;
            if (dif == 0) {
                cell.data = item;
                cell.seq.store(pos + 1, std::memory_order_release);
                return;
            }
            std::this_thread::yield();
        }
    }

    // Try enqueue: attempts to reserve and write, returns false if queue looks full.
    bool try_enqueue(const T& item)
    {
        size_t pos = tail_.load(std::memory_order_relaxed);
        for (;;) {
            Cell &cell = buffer_[pos & mask_];
            size_t seq = cell.seq.load(std::memory_order_acquire);
            long long dif = (long long)seq - (long long)pos;
            if (dif == 0) {
                if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    cell.data = item;
                    cell.seq.store(pos + 1, std::memory_order_release);
                    return true;
                }
                // CAS failed: pos now contains updated tail, retry
                continue;
            }
            return false;
        }
    }

    // Blocking dequeue: reserve a ticket then wait for the slot to be filled.
    void dequeue(T& out)
    {
        const size_t pos = head_.fetch_add(1, std::memory_order_relaxed);
        Cell &cell = buffer_[pos & mask_];
        for (;;) {
            size_t seq = cell.seq.load(std::memory_order_acquire);
            long long dif = (long long)seq - (long long)(pos + 1);
            if (dif == 0) {
                out = cell.data;
                cell.seq.store(pos + cap_, std::memory_order_release);
                return;
            }
            std::this_thread::yield();
        }
    }

    // Try dequeue: returns false if empty.
    bool try_dequeue(T& out)
    {
        size_t pos = head_.load(std::memory_order_relaxed);
        for (;;) {
            Cell &cell = buffer_[pos & mask_];
            size_t seq = cell.seq.load(std::memory_order_acquire);
            long long dif = (long long)seq - (long long)(pos + 1);
            if (dif == 0) {
                if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    out = cell.data;
                    cell.seq.store(pos + cap_, std::memory_order_release);
                    return true;
                }
                continue;
            }
            return false;
        }
    }

    size_t capacity() const noexcept { return cap_; }

private:
    struct Cell {
        std::atomic<size_t> seq;
        T data;
        Cell() noexcept : seq(0), data() {}
        ~Cell() {}
    };

    size_t cap_;
    size_t mask_;
    std::unique_ptr<Cell[]> buffer_;
    std::atomic<size_t> head_; // consumer index
    std::atomic<size_t> tail_; // producer index
};
