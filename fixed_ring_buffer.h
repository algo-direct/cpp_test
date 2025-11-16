#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <array>

/**
 * @brief Lock-free single-producer single-consumer (SPSC) fixed-size ring buffer
 * 
 * This is a high-performance, wait-free ring buffer designed for single producer
 * and single consumer scenarios. It uses atomic operations for synchronization.
 * 
 * Key features:
 * - Wait-free operations (no blocking, no locks)
 * - Cache-line aligned to avoid false sharing
 * - Power-of-2 size for fast modulo operations
 * - Suitable for inter-thread communication
 * 
 * @tparam T Type of elements stored in the buffer
 * @tparam Capacity Size of the buffer (must be power of 2)
 */
template<typename T, size_t Capacity>
class FixedRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static_assert(Capacity > 0, "Capacity must be greater than 0");

private:
    // Align to cache line (64 bytes on most modern CPUs) to avoid false sharing
    alignas(64) std::atomic<uint64_t> write_pos_{0};
    alignas(64) std::atomic<uint64_t> read_pos_{0};
    alignas(64) std::array<T, Capacity> buffer_;
    
    static constexpr uint64_t INDEX_MASK = Capacity - 1;

public:
    FixedRingBuffer() = default;
    
    // Disable copy and move to avoid complexity with atomics
    FixedRingBuffer(const FixedRingBuffer&) = delete;
    FixedRingBuffer& operator=(const FixedRingBuffer&) = delete;
    FixedRingBuffer(FixedRingBuffer&&) = delete;
    FixedRingBuffer& operator=(FixedRingBuffer&&) = delete;

    /**
     * @brief Push an element into the ring buffer (producer side)
     * 
     * @param item Element to push
     * @return true if successful, false if buffer is full
     */
    bool push(const T& item) {
        const uint64_t current_write = write_pos_.load(std::memory_order_relaxed);
        const uint64_t next_write = current_write + 1;
        
        // Check if buffer is full
        // We leave one slot empty to distinguish full from empty
        if (next_write == read_pos_.load(std::memory_order_acquire) + Capacity) {
            return false;
        }
        
        buffer_[current_write & INDEX_MASK] = item;
        write_pos_.store(next_write, std::memory_order_release);
        return true;
    }

    /**
     * @brief Push an element into the ring buffer (move semantics)
     * 
     * @param item Element to push
     * @return true if successful, false if buffer is full
     */
    bool push(T&& item) {
        const uint64_t current_write = write_pos_.load(std::memory_order_relaxed);
        const uint64_t next_write = current_write + 1;
        
        if (next_write == read_pos_.load(std::memory_order_acquire) + Capacity) {
            return false;
        }
        
        buffer_[current_write & INDEX_MASK] = std::move(item);
        write_pos_.store(next_write, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pop an element from the ring buffer (consumer side)
     * 
     * @return std::optional<T> containing the element if available, nullopt if empty
     */
    std::optional<T> pop() {
        const uint64_t current_read = read_pos_.load(std::memory_order_relaxed);
        
        // Check if buffer is empty
        if (current_read == write_pos_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        
        T item = std::move(buffer_[current_read & INDEX_MASK]);
        read_pos_.store(current_read + 1, std::memory_order_release);
        return item;
    }

    /**
     * @brief Peek at the front element without removing it
     * 
     * @return std::optional<T> containing a copy of the front element if available
     */
    std::optional<T> peek() const {
        const uint64_t current_read = read_pos_.load(std::memory_order_relaxed);
        
        if (current_read == write_pos_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        
        return buffer_[current_read & INDEX_MASK];
    }

    /**
     * @brief Check if the ring buffer is empty
     * 
     * @return true if empty, false otherwise
     */
    bool empty() const {
        return read_pos_.load(std::memory_order_acquire) == 
               write_pos_.load(std::memory_order_acquire);
    }

    /**
     * @brief Check if the ring buffer is full
     * 
     * @return true if full, false otherwise
     */
    bool full() const {
        const uint64_t write = write_pos_.load(std::memory_order_acquire);
        const uint64_t read = read_pos_.load(std::memory_order_acquire);
        return (write + 1) == read + Capacity;
    }

    /**
     * @brief Get the current number of elements in the buffer
     * 
     * Note: This is an approximation in a concurrent context
     * 
     * @return size_t Number of elements
     */
    size_t size() const {
        const uint64_t write = write_pos_.load(std::memory_order_acquire);
        const uint64_t read = read_pos_.load(std::memory_order_acquire);
        return static_cast<size_t>(write - read);
    }

    /**
     * @brief Get the capacity of the ring buffer
     * 
     * @return size_t Maximum number of elements
     */
    constexpr size_t capacity() const {
        return Capacity;
    }

    /**
     * @brief Get available space in the buffer
     * 
     * @return size_t Number of elements that can be pushed
     */
    size_t available() const {
        return Capacity - size();
    }

    /**
     * @brief Clear all elements from the buffer
     * 
     * WARNING: Not thread-safe! Only call when you have exclusive access
     */
    void clear() {
        read_pos_.store(0, std::memory_order_relaxed);
        write_pos_.store(0, std::memory_order_relaxed);
    }
};


/**
 * @brief Multi-producer multi-consumer (MPMC) fixed-size ring buffer
 * 
 * This version uses CAS operations to support multiple producers and consumers.
 * It has higher overhead than SPSC but is safe for concurrent access from multiple threads.
 * 
 * @tparam T Type of elements stored in the buffer
 * @tparam Capacity Size of the buffer (must be power of 2)
 */
template<typename T, size_t Capacity>
class MPMCFixedRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static_assert(Capacity > 0, "Capacity must be greater than 0");

private:
    struct Slot {
        alignas(64) std::atomic<uint64_t> sequence{0};
        T data;
    };

    alignas(64) std::atomic<uint64_t> enqueue_pos_{0};
    alignas(64) std::atomic<uint64_t> dequeue_pos_{0};
    alignas(64) std::array<Slot, Capacity> buffer_;
    
    static constexpr uint64_t INDEX_MASK = Capacity - 1;

public:
    MPMCFixedRingBuffer() {
        for (size_t i = 0; i < Capacity; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }
    
    MPMCFixedRingBuffer(const MPMCFixedRingBuffer&) = delete;
    MPMCFixedRingBuffer& operator=(const MPMCFixedRingBuffer&) = delete;
    MPMCFixedRingBuffer(MPMCFixedRingBuffer&&) = delete;
    MPMCFixedRingBuffer& operator=(MPMCFixedRingBuffer&&) = delete;

    /**
     * @brief Push an element (thread-safe for multiple producers)
     */
    bool push(const T& item) {
        uint64_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        
        for (;;) {
            Slot& slot = buffer_[pos & INDEX_MASK];
            uint64_t seq = slot.sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            
            if (diff == 0) {
                // Slot is available, try to claim it
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, 
                    std::memory_order_relaxed)) {
                    slot.data = item;
                    slot.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                // Buffer is full
                return false;
            } else {
                // Another thread is working on this slot, try next position
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

    /**
     * @brief Push an element with move semantics (thread-safe)
     */
    bool push(T&& item) {
        uint64_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        
        for (;;) {
            Slot& slot = buffer_[pos & INDEX_MASK];
            uint64_t seq = slot.sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            
            if (diff == 0) {
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, 
                    std::memory_order_relaxed)) {
                    slot.data = std::move(item);
                    slot.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

    /**
     * @brief Pop an element (thread-safe for multiple consumers)
     */
    std::optional<T> pop() {
        uint64_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        
        for (;;) {
            Slot& slot = buffer_[pos & INDEX_MASK];
            uint64_t seq = slot.sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
            
            if (diff == 0) {
                // Slot is ready, try to claim it
                if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, 
                    std::memory_order_relaxed)) {
                    T item = std::move(slot.data);
                    slot.sequence.store(pos + Capacity, std::memory_order_release);
                    return item;
                }
            } else if (diff < 0) {
                // Buffer is empty
                return std::nullopt;
            } else {
                // Another thread is working on this slot
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

    /**
     * @brief Check if buffer is empty (approximation in concurrent context)
     */
    bool empty() const {
        uint64_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        const Slot& slot = buffer_[pos & INDEX_MASK];
        uint64_t seq = slot.sequence.load(std::memory_order_acquire);
        return static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1) < 0;
    }

    /**
     * @brief Get approximate size (may not be exact in concurrent context)
     */
    size_t size() const {
        uint64_t enq = enqueue_pos_.load(std::memory_order_acquire);
        uint64_t deq = dequeue_pos_.load(std::memory_order_acquire);
        return static_cast<size_t>(enq - deq);
    }

    constexpr size_t capacity() const {
        return Capacity;
    }
};

