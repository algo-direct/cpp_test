#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <chrono>
#include <memory>
#include <new>
#include <vector>
#include <type_traits>

// Bounded multi-producer multi-consumer queue (Vyukov MPMC bounded queue).
// - Capacity rounded up to power-of-two.
// - Multiple producers and multiple consumers supported.
// Uses per-slot sequence numbers and atomic head/tail with CAS on reservation.
template<typename T, size_t InitialCapacity = 1024>
class MPMCQueue {
    
private:
    struct Cell {
        // put the sequence on its own cache-line where possible to reduce false sharing
        alignas(64) std::atomic<size_t> seq;
        // data follows; placing seq first helps keep seq updates isolated
        T data;
        Cell() noexcept : seq(0), data() {}
        ~Cell() {}
    };

    // cap_ and mask_ are compile-time inline static constexpr members (see above)
    // raw aligned storage for Cells; we placement-new Cells into this vector
    std::vector<std::aligned_storage_t<sizeof(Cell), alignof(Cell)>> storage_;
    // align head and tail to separate cache lines to reduce contention
    alignas(64) std::atomic<size_t> head_; // consumer index
    alignas(64) std::atomic<size_t> tail_; // producer index
    // Lightweight instrumentation
    std::atomic<uint64_t> stats_spins_{0};
    std::atomic<uint64_t> stats_cas_failures_{0};
    
public:
    // compute next power-of-two at compile time from template parameter `capacity`
    static consteval size_t round_up_pow2(size_t n) noexcept {
        size_t v = 1u;
        while (v < n) v <<= 1u;
        return v;
    }

    // cap_ and mask_ are compile-time constants derived from the template param
    inline static constexpr size_t cap_ = round_up_pow2(InitialCapacity);
    inline static constexpr size_t mask_ = cap_ - 1u;


    // signed difference type matching size_t width
    using diff_t = std::make_signed_t<size_t>;
    static_assert(std::is_signed_v<diff_t>, "diff_t must be signed");

    // CPU-relax/backoff helper: progressive backoff using pause/yield/sleep
    static inline void cpu_relax(int &spin)
    {
#if defined(__x86_64__) || defined(__i386__)
        if (spin < 10) {
            // brief pause to reduce pipeline pressure
            asm volatile("pause" ::: "memory");
        } else
#endif
        if (spin < 30) {
            // yield to scheduler
            std::this_thread::yield();
        } else {
            // sleep a short time to avoid burning CPU if contention persists
            std::this_thread::sleep_for(std::chrono::nanoseconds(100));
        }
        ++spin;
    }

    explicit MPMCQueue()
    {
        // allocate raw aligned storage for Cells and placement-new them
        using storage_t = std::aligned_storage_t<sizeof(Cell), alignof(Cell)>;
        storage_.resize(cap_);
        for (size_t i = 0; i < cap_; ++i) {
            void* ptr = reinterpret_cast<void*>(&storage_[i]);
            new (ptr) Cell();
            reinterpret_cast<Cell*>(ptr)->seq.store(i, std::memory_order_relaxed);
        }
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    ~MPMCQueue()
    {
        // destroy cells (they were placement-new'ed into aligned storage)
        for (size_t i = 0; i < cap_; ++i) {
            void* ptr = reinterpret_cast<void*>(&storage_[i]);
            reinterpret_cast<Cell*>(ptr)->~Cell();
        }
    }

    MPMCQueue(const MPMCQueue&) = delete;
    MPMCQueue& operator=(const MPMCQueue&) = delete;

    inline Cell & cell_at(size_t i) noexcept {
        return *reinterpret_cast<Cell*>(&storage_[i]);
    }

    // Blocking enqueue: reserve a ticket then wait for the slot to become available.
    void enqueue(const T& item)
    {
        const size_t pos = tail_.fetch_add(1, std::memory_order_relaxed);
        Cell &cell = cell_at(pos & mask_);
        int spin = 0;
        for (;;) {
            size_t seq = cell.seq.load(std::memory_order_acquire);
            diff_t dif = static_cast<diff_t>(seq) - static_cast<diff_t>(pos);
            if (dif == 0) {
                cell.data = item;
                cell.seq.store(pos + 1, std::memory_order_release);
                return;
            }
            // backoff
            stats_spins_.fetch_add(1, std::memory_order_relaxed);
            cpu_relax(spin);
        }
    }

    // Try enqueue: attempts to reserve and write, returns false if queue looks full.
    bool try_enqueue(const T& item)
    {
        size_t pos = tail_.load(std::memory_order_relaxed);
        for (;;) {
            Cell &cell = cell_at(pos & mask_);
            size_t seq = cell.seq.load(std::memory_order_acquire);
            diff_t dif = static_cast<diff_t>(seq) - static_cast<diff_t>(pos);
            if (dif == 0) {
                if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    cell.data = item;
                    cell.seq.store(pos + 1, std::memory_order_release);
                    return true;
                }
                // CAS failed: record and retry
                stats_cas_failures_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            return false;
        }
    }

    // Blocking dequeue: reserve a ticket then wait for the slot to be filled.
    void dequeue(T& out)
    {
        const size_t pos = head_.fetch_add(1, std::memory_order_relaxed);
        Cell &cell = cell_at(pos & mask_);
        int spin = 0;
        for (;;) {
            size_t seq = cell.seq.load(std::memory_order_acquire);
            diff_t dif = static_cast<diff_t>(seq) - static_cast<diff_t>(pos + 1);
            if (dif == 0) {
                out = cell.data;
                cell.seq.store(pos + cap_, std::memory_order_release);
                return;
            }
            stats_spins_.fetch_add(1, std::memory_order_relaxed);
            cpu_relax(spin);
        }
    }

    // Try dequeue: returns false if empty.
    bool try_dequeue(T& out)
    {
        size_t pos = head_.load(std::memory_order_relaxed);
        for (;;) {
            Cell &cell = cell_at(pos & mask_);
            size_t seq = cell.seq.load(std::memory_order_acquire);
            diff_t dif = static_cast<diff_t>(seq) - static_cast<diff_t>(pos + 1);
            if (dif == 0) {
                if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    out = cell.data;
                    cell.seq.store(pos + cap_, std::memory_order_release);
                    return true;
                }
                stats_cas_failures_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            return false;
        }
    }

    size_t capacity() const noexcept { return cap_; }

    // instrumentation accessors
    uint64_t stats_spins() const noexcept { return stats_spins_.load(std::memory_order_relaxed); }
    uint64_t stats_cas_failures() const noexcept { return stats_cas_failures_.load(std::memory_order_relaxed); }

};
