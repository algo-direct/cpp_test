#pragma once
#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

// Simple single-producer single-consumer lock-free ring buffer.
// - Capacity must be a power of two (we round up if not).
// - Not thread-safe for multiple producers or multiple consumers.
template<typename T>
class SPSCQueue {
public:
    explicit SPSCQueue(size_t capacity)
    {
        cap_ = 1u;
        while (cap_ < capacity) cap_ <<= 1u;
        mask_ = cap_ - 1u;
        buf_.reset(new T[cap_]);
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // Enqueue an item. Returns false if queue is full.
    bool enqueue(const T& item)
    {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);
        if ((tail - head) >= cap_) return false; // full
        buf_[tail & mask_] = item;
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // Try dequeue an item. Returns false if queue is empty.
    bool dequeue(T& item)
    {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_acquire);
        if (tail == head) return false; // empty
        item = buf_[head & mask_];
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // Non-atomic helpers for testing/inspection
    size_t capacity() const noexcept { return cap_; }
    size_t size() const noexcept { return tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire); }

private:
    size_t cap_;
    size_t mask_;
    std::unique_ptr<T[]> buf_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};
