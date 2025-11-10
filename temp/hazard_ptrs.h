// Minimal hazard-pointer scaffolding for educational use.
// This header provides a tiny API for guarding pointers and retiring objects.
// It's intentionally small and not optimized; for production use prefer a
// battle-tested implementation (e.g. Concurrency Kit, Folly, or a robust HP lib).

#pragma once

#include <atomic>
#include <vector>
#include <functional>

namespace hp {
    constexpr int MAX_HP_THREADS = 256;
    static std::atomic<void*> hp_list[MAX_HP_THREADS];
    static std::atomic<int> hp_next{0};

    thread_local int hp_slot = -1;
    thread_local std::vector<void*> hp_retire;

    inline int alloc_slot() {
        int s = hp_next.fetch_add(1, std::memory_order_relaxed);
        if (s >= MAX_HP_THREADS) return s % MAX_HP_THREADS;
        return s;
    }

    struct Guard {
        Guard(void* p = nullptr) {
            if (hp_slot == -1) hp_slot = alloc_slot();
            set(p);
        }
        void set(void* p) { hp_list[hp_slot].store(p, std::memory_order_release); }
        void clear() { hp_list[hp_slot].store(nullptr, std::memory_order_release); }
        ~Guard() { clear(); }
    };

    inline void retire(void* p, std::function<void(void*)> deleter) {
        if (hp_slot == -1) hp_slot = alloc_slot();
        hp_retire.push_back(p);
        const size_t THRESH = 64;
        if (hp_retire.size() >= THRESH) {
            std::vector<void*> hazards;
            hazards.reserve(MAX_HP_THREADS);
            for (int i = 0; i < MAX_HP_THREADS; ++i) {
                void* h = hp_list[i].load(std::memory_order_acquire);
                if (h) hazards.push_back(h);
            }
            std::vector<void*> remaining;
            for (void* r : hp_retire) {
                bool in_use = false;
                for (void* h : hazards) if (h == r) { in_use = true; break; }
                if (in_use) remaining.push_back(r);
                else deleter(r);
            }
            hp_retire.swap(remaining);
        }
    }
}

// Usage notes:
// - Use hp::Guard g(ptr) to publish a pointer you intend to dereference.
// - After you finish, the Guard destructor clears the slot.
// - Call hp::retire(ptr, deleter) to retire nodes for safe reclamation.
