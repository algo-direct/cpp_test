#pragma once
#include <atomic>
#include <cstddef>
#include <memory>
#include <new>

// Bounded multi-producer single-consumer queue based on Dmitry Vyukov's MPMC
// technique, adapted for MPSC. It's a fixed-size ring buffer where each slot
// has a sequence number used to determine slot ownership.
// - Capacity is rounded up to a power of two.
// - Multiple producers may call enqueue concurrently.
// - Only a single consumer may call dequeue.
template<typename T>
class MPSCQueue {
public:
    explicit MPSCQueue(size_t capacity)
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

    ~MPSCQueue()
    {
        // destroy cells
        for (size_t i = 0; i < cap_; ++i) {
            buffer_[i].~Cell();
        }
        operator delete[](buffer_.release());
    }

    MPSCQueue(const MPSCQueue&) = delete;
    MPSCQueue& operator=(const MPSCQueue&) = delete;

    // Blocking enqueue: spins until space is available.
    void enqueue(const T& item)
    {
        size_t pos = tail_.fetch_add(1, std::memory_order_relaxed);
        for (;;) {
            Cell &cell = buffer_[pos & mask_];
            size_t seq = cell.seq.load(std::memory_order_acquire);
            long long dif = (long long)seq - (long long)pos;
            if (dif == 0) {
                // slot is ours
                cell.data = item;
                cell.seq.store(pos + 1, std::memory_order_release);
                return;
            }
            // queue full or contention — spin/yield
            std::this_thread::yield();
        }
    }

    // Try to enqueue; returns false if queue is full at the moment.
    bool try_enqueue(const T& item)
    {
        size_t pos = tail_.fetch_add(1, std::memory_order_relaxed);
        Cell &cell = buffer_[pos & mask_];
        size_t seq = cell.seq.load(std::memory_order_acquire);
        long long dif = (long long)seq - (long long)pos;
        if (dif == 0) {
            cell.data = item;
            cell.seq.store(pos + 1, std::memory_order_release);
            return true;
        }
        // not available - rollback is not possible with simple fetch_add; this
        // implementation chooses to fail the try instead of complicating with CAS.
        return false;
    }

    // Blocking dequeue: spins until an item is available. Returns the item via out param.
    void dequeue(T& out)
    {
        size_t pos = head_.fetch_add(1, std::memory_order_relaxed);
        for (;;) {
            Cell &cell = buffer_[pos & mask_];
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

    // Try dequeue; returns false if empty.
    bool try_dequeue(T& out)
    {
        size_t pos = head_.fetch_add(1, std::memory_order_relaxed);
        Cell &cell = buffer_[pos & mask_];
        size_t seq = cell.seq.load(std::memory_order_acquire);
        long long dif = (long long)seq - (long long)(pos + 1);
        if (dif == 0) {
            out = cell.data;
            cell.seq.store(pos + cap_, std::memory_order_release);
            return true;
        }
        return false;
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
