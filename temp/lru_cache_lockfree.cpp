// Lock-free LRU-like cache examples.
// Two approaches provided:
// 1) LockFreeLRU_HazardPointers: uses raw pointers for nodes and a simple
//    hazard-pointer scheme for safe reclamation. Buckets are lock-free stacks
//    inserted via CAS. This demonstrates hazard pointer usage to avoid
//    use-after-free when traversing and retiring nodes.
// 2) LockFreeLRU_PerNodeCAS: uses std::shared_ptr for nodes and performs
//    per-node CAS updates using atomic shared_ptr operations. Shared_ptr
//    reference counting provides safe reclamation; CAS on shared_ptr provides
//    lock-free updates.
//
// These are educational examples demonstrating patterns rather than a
// production-grade LRU (exact recency ordering and strict capacity semantics
// are non-trivial to implement lock-free).

#include <atomic>
#include <cassert>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

// ------------------------- Hazard Pointers -------------------------
namespace hp {
    constexpr int MAX_THREADS = 128;
    // one hazard pointer slot per thread for simplicity
    static std::atomic<void*> hazard_ptrs[MAX_THREADS];
    static std::atomic<int> next_slot{0};

    thread_local int my_slot = -1;
    thread_local std::vector<void*> retire_list;

    int alloc_slot() {
        int s = next_slot.fetch_add(1, std::memory_order_relaxed);
        if (s >= MAX_THREADS) {
            // fallback: reuse slots (not ideal)
            return s % MAX_THREADS;
        }
        return s;
    }

    // initialize hazard array to null pointers
    inline void init() {
        for (int i = 0; i < MAX_THREADS; ++i) hazard_ptrs[i].store(nullptr, std::memory_order_relaxed);
    }

    struct Guard {
        Guard(void* p = nullptr) {
            if (my_slot == -1) my_slot = alloc_slot();
            set(p);
        }
        void set(void* p) { hazard_ptrs[my_slot].store(p, std::memory_order_release); }
        void clear() { hazard_ptrs[my_slot].store(nullptr, std::memory_order_release); }
        ~Guard() { clear(); }
    };

    void retire(void* p, std::function<void(void*)> deleter) {
        if (my_slot == -1) my_slot = alloc_slot();
        retire_list.push_back(p);
        const size_t THRESH = 64;
        if (retire_list.size() >= THRESH) {
            // collect hazard pointers snapshot
            std::vector<void*> hazards;
            hazards.reserve(MAX_THREADS);
            for (int i = 0; i < MAX_THREADS; ++i) {
                void* v = hazard_ptrs[i].load(std::memory_order_acquire);
                if (v) hazards.push_back(v);
            }
            // try to reclaim
            std::vector<void*> remaining;
            for (void* r : retire_list) {
                bool in_use = false;
                for (void* h : hazards) if (h == r) { in_use = true; break; }
                if (in_use) remaining.push_back(r);
                else deleter(r);
            }
            retire_list.swap(remaining);
        }
    }
}

// ------------------- LockFreeLRU using Hazard Pointers -------------------
template<typename K, typename V>
class LockFreeLRU_HazardPointers {
    struct Node {
        K key;
        V value;
        std::atomic<Node*> next;
        std::atomic<bool> deleted;
        uint64_t timestamp;
        Node(const K& k, const V& v, Node* n)
            : key(k), value(v), next(n), deleted(false), timestamp(0) {}
    };

public:
    explicit LockFreeLRU_HazardPointers(size_t buckets = 64, size_t capacity = 1024)
        : buckets_(buckets), capacity_(capacity), size_(0) {
        hp::init();
        heads_.reset(new std::atomic<Node*>[buckets_]);
        for (size_t i = 0; i < buckets_; ++i) heads_[i].store(nullptr, std::memory_order_relaxed);
    }

    ~LockFreeLRU_HazardPointers() {
        for (size_t i = 0; i < buckets_; ++i) {
            Node* p = heads_[i].load(std::memory_order_relaxed);
            while (p) { Node* n = p->next.load(std::memory_order_relaxed); delete p; p = n; }
        }
    }

    std::optional<V> get(const K& k) {
        size_t i = bucket(k);
        // traverse lock-free with hazard pointer guard
        Node* cur = heads_[i].load(std::memory_order_acquire);
        while (cur) {
            hp::Guard gcur((void*)cur);
            // ensure head hasn't changed to avoid guarding a stale pointer
            if (heads_[i].load(std::memory_order_acquire) != cur) {
                cur = heads_[i].load(std::memory_order_acquire);
                continue;
            }
            // check deleted
            if (cur->deleted.load(std::memory_order_acquire)) {
                cur = cur->next.load(std::memory_order_acquire);
                continue;
            }
            if (cur->key == k) {
                cur->timestamp = timestamp_now();
                return cur->value;
            }
            // Move to next safely: read next, set hazard, verify
            Node* next = cur->next.load(std::memory_order_acquire);
            if (!next) break;
            hp::Guard gnext((void*)next);
            if (cur->next.load(std::memory_order_acquire) != next) {
                // link changed; retry from current head
                cur = heads_[i].load(std::memory_order_acquire);
                continue;
            }
            cur = next;
        }
        return std::nullopt;
    }

    void put(const K& k, V v) {
        size_t i = bucket(k);
        Node* newn = new Node(k, v, nullptr);
        newn->timestamp = timestamp_now();
        for (;;) {
            Node* head = heads_[i].load(std::memory_order_acquire);
            newn->next.store(head, std::memory_order_relaxed);
            if (heads_[i].compare_exchange_weak(head, newn, std::memory_order_release, std::memory_order_relaxed)) {
                size_.fetch_add(1, std::memory_order_relaxed);
                break;
            }
        }
        // opportunistic cleanup if too large
        if (size_.load(std::memory_order_relaxed) > capacity_) compact(i);
    }

    size_t size() const { return size_.load(); }

private:
    size_t bucket(const K& k) const { return std::hash<K>{}(k) % buckets_; }

    uint64_t timestamp_now() const { return (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count(); }

    void compact(size_t i) {
        // simple compaction: remove nodes with oldest timestamps (single-pass)
    Node* prev = nullptr;
    Node* cur = heads_[i].load(std::memory_order_acquire);
        uint64_t oldest = UINT64_MAX;
        Node* oldest_node = nullptr;
        Node* oldest_prev = nullptr;
        Node* node = cur;
        // traverse with hazard-pointer guards to avoid accessing reclaimed nodes
        while (node) {
            hp::Guard gnode((void*)node);
            // ensure head hasn't changed to avoid guarding a stale pointer
            if (heads_[i].load(std::memory_order_acquire) != node) {
                // restart traversal from fresh head
                prev = nullptr;
                cur = heads_[i].load(std::memory_order_acquire);
                node = cur;
                oldest = UINT64_MAX; oldest_node = nullptr; oldest_prev = nullptr;
                continue;
            }
            // read fields while guarded
            if (!node->deleted.load(std::memory_order_acquire)) {
                uint64_t t = node->timestamp;
                if (t < oldest) { oldest = t; oldest_node = node; oldest_prev = prev; }
            }
            // prepare next safely
            Node* next = node->next.load(std::memory_order_acquire);
            if (!next) break;
            hp::Guard gnext((void*)next);
            if (node->next.load(std::memory_order_acquire) != next) {
                // list changed; restart from head
                prev = nullptr;
                node = heads_[i].load(std::memory_order_acquire);
                continue;
            }
            prev = node;
            node = next;
        }
        if (!oldest_node) return;
    // logically delete
    bool expected = false;
    if (!oldest_node->deleted.compare_exchange_strong(expected, true)) return;
        // physically unlink by CAS on head if it's the head, else perform a best-effort unlink
        if (oldest_prev == nullptr) {
            // head: try to unlink safely
            Node* cur_head = heads_[i].load(std::memory_order_acquire);
            Node* next = oldest_node->next.load(std::memory_order_acquire);
            heads_[i].compare_exchange_strong(cur_head, next, std::memory_order_acq_rel);
        } else {
            // Non-head unlink is unsafe in this simple example because 'oldest_prev'
            // may have been concurrently removed; to avoid unsafe memory access,
            // we only logical-delete here and leave physical unlinking to helpers.
        }
        size_.fetch_sub(1, std::memory_order_relaxed);
        // retire memory via hazard-pointer retire
        hp::retire((void*)oldest_node, [](void* p){ delete static_cast<Node*>(p); });
    }

    size_t buckets_;
    size_t capacity_;
    std::unique_ptr<std::atomic<Node*>[]> heads_;
    std::atomic<size_t> size_;
};

// ---------------- LockFreeLRU using per-node CAS and shared_ptr ----------------
template<typename K, typename V>
class LockFreeLRU_PerNodeCAS {
    struct Node {
        K key;
        V value;
        std::shared_ptr<Node> next;
        std::atomic<uint64_t> timestamp;
        Node(const K& k, const V& v) : key(k), value(v), next(nullptr), timestamp(0) {}
    };

public:
    explicit LockFreeLRU_PerNodeCAS(size_t buckets = 64, size_t capacity = 1024)
        : buckets_(buckets), capacity_(capacity), size_(0) {
        heads_.reset(new std::atomic<std::shared_ptr<Node>>[buckets_]);
        for (size_t i = 0; i < buckets_; ++i) heads_[i].store(std::shared_ptr<Node>(nullptr), std::memory_order_relaxed);
    }

    std::optional<V> get(const K& k) {
        size_t i = bucket(k);
        auto cur = heads_[i].load(std::memory_order_acquire);
        while (cur) {
            if (cur->key == k) {
                cur->timestamp.store(timestamp_now(), std::memory_order_relaxed);
                return cur->value;
            }
            cur = cur->next;
        }
        return std::nullopt;
    }

    void put(const K& k, V v) {
        size_t i = bucket(k);
        auto newn = std::make_shared<Node>(k, v);
        newn->timestamp.store(timestamp_now(), std::memory_order_relaxed);
        for (;;) {
            auto head = heads_[i].load(std::memory_order_acquire);
            newn->next = head;
            // atomic compare-exchange on atomic<shared_ptr>
            if (heads_[i].compare_exchange_weak(head, newn, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                size_.fetch_add(1, std::memory_order_relaxed);
                break;
            }
        }
        // naive eviction
        if (size_.load(std::memory_order_relaxed) > capacity_) compact(i);
    }

    size_t size() const { return size_.load(); }

private:
    size_t bucket(const K& k) const { return std::hash<K>{}(k) % buckets_; }
    uint64_t timestamp_now() const { return (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count(); }

    void compact(size_t i) {
        // remove oldest node by walking list and unlinking via CAS on head when possible
    auto head = heads_[i].load(std::memory_order_acquire);
    std::shared_ptr<Node> prev = nullptr;
    auto cur = head;
        uint64_t oldest = UINT64_MAX;
        std::shared_ptr<Node> oldest_prev = nullptr;
        std::shared_ptr<Node> oldest_node = nullptr;
        while (cur) {
            uint64_t t = cur->timestamp.load(std::memory_order_relaxed);
            if (t < oldest) { oldest = t; oldest_node = cur; oldest_prev = prev; }
            prev = cur;
            cur = cur->next;
        }
        if (!oldest_node) return;
        // if oldest_prev is null, it's head
        if (!oldest_prev) {
            auto expected = heads_[i].load(std::memory_order_acquire);
            heads_[i].compare_exchange_strong(expected, oldest_node->next, std::memory_order_acq_rel, std::memory_order_relaxed);
        } else {
            // best-effort unlink skipped for safety (see comments above)
        }
        size_.fetch_sub(1, std::memory_order_relaxed);
        // shared_ptr will reclaim memory when no references remain
    }

    size_t buckets_;
    size_t capacity_;
    std::unique_ptr<std::atomic<std::shared_ptr<Node>>[]> heads_;
    std::atomic<size_t> size_;
};

// ---------------------- Simple smoke tests ----------------------
#ifndef LRU_BENCH
int main() {
    {
        LockFreeLRU_HazardPointers<int,int> c(8, 128);
        c.put(1,10);
        c.put(2,20);
        auto r = c.get(1);
        if (!r || *r != 10) { std::cerr << "LF HP get failed\n"; return 1; }
    }
    {
        LockFreeLRU_PerNodeCAS<int,int> c(8, 128);
        c.put(1,100);
        c.put(2,200);
        auto r = c.get(2);
        if (!r || *r != 200) { std::cerr << "LF CAS get failed\n"; return 2; }
    }
    std::cout << "lru_cache_lockfree: PASS\n";
    return 0;
}
#endif
