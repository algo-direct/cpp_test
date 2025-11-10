// Model solution: SPSC ring buffer (lock-free) with power-of-two capacity
#include <atomic>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <vector>

template<typename T>
class SPSCQueue {
public:
    explicit SPSCQueue(size_t capacity)
    : cap_(round_pow2(std::max<size_t>(2, capacity))), mask_(cap_ - 1)
    {
        buffer_.resize(cap_);
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    bool push(const T& v) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);
        if (tail - head >= cap_) return false; // full
        buffer_[tail & mask_] = v;
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& out) {
        const size_t head = head_.load(std::memory_order_relaxed);
        if (tail_.load(std::memory_order_acquire) == head) return false; // empty
        out = buffer_[head & mask_];
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

private:
    static size_t round_pow2(size_t v) {
        v--; v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16; 
        if (sizeof(size_t) > 4) v |= v >> 32;
        return v + 1;
    }

    const size_t cap_;
    const size_t mask_;
    std::vector<T> buffer_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};

int main() {
    const int N = 10000;
    SPSCQueue<int> q((size_t)N + 4);
    for (int i = 0; i < N; ++i) {
        bool ok = q.push(i);
        if (!ok) {
            std::cerr << "push failed at " << i << "\n";
            return 2;
        }
    }
    for (int i = 0; i < N; ++i) {
        int v;
        bool ok = q.pop(v);
        if (!ok) { std::cerr << "pop failed at " << i << "\n"; return 3; }
        if (v != i) { std::cerr << "mismatch: " << v << " != " << i << "\n"; return 4; }
    }
    std::cout << "spsc_ring: PASS\n";
    return 0;
}
