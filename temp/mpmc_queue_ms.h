// Michael & Scott lock-free MPMC queue (header)
//
// This header contains the MPMCQueue class used by multiple example programs.
// It documents the invariants, memory-ordering decisions, and when it is
// safe to reclaim nodes. The implementation follows the classic Michael & Scott
// algorithm: a singly-linked list with a dummy head node, and two atomics
// (head and tail). Enqueue performs a link-then-advance-tail. Dequeue advances
// head and reclaims the old dummy node.

#pragma once

#include <atomic>
#include <utility>

template<typename T>
class MPMCQueue {
    struct Node {
        std::atomic<Node*> next;
        T value;
        Node(T&& v) : next(nullptr), value(std::move(v)) {}
        Node() : next(nullptr), value() {}
    };

public:
    MPMCQueue() {
        // create a single dummy node: head and tail both point to it.
        // head always points to a node whose next is the first real element
        // (or nullptr when empty). This simplifies empty checks and pop.
        Node* dummy = new Node();
        head_.store(dummy, std::memory_order_relaxed);
        tail_.store(dummy, std::memory_order_relaxed);
    }

    ~MPMCQueue() {
        // Drain and delete remaining nodes. In the classic M&S algorithm
        // each successful consumer that advances head deletes the previous
        // dummy node; here we also free anything still reachable at
        // destruction time.
        Node* n = head_.load(std::memory_order_relaxed);
        while (n) {
            Node* next = n->next.load(std::memory_order_relaxed);
            delete n;
            n = next;
        }
    }

    // push accepts lvalues and rvalues. It allocates a new node, then tries
    // to link it at tail->next with a CAS. After linking, it attempts to
    // advance the tail pointer (best-effort). The link CAS is the linearize
    // point for enqueue; advancing tail is only an optimization to keep tail
    // from lagging.
    void push(const T& v) { push_impl(T(v)); }
    void push(T&& v) { push_impl(std::move(v)); }

    // try_pop attempts to remove one element. It reads head, tail and
    // head->next. If head==tail and next==nullptr the queue is empty. If a
    // node exists, a CAS on head (acq_rel) advances the head to the next
    // node and the old dummy can be safely deleted by the successful thread.
    // This deletion is safe in the classic M&S algorithm because once a
    // thread has successfully swapped head, no other thread will access the
    // old head via following head pointer semantics.
    bool try_pop(T& out) {
        while (true) {
            Node* head = head_.load(std::memory_order_acquire);
            Node* tail = tail_.load(std::memory_order_acquire);
            Node* next = head->next.load(std::memory_order_acquire);
            if (head == tail) {
                if (next == nullptr) return false; // empty
                // tail falling behind, try to advance it (helping)
                tail_.compare_exchange_weak(tail, next, std::memory_order_release, std::memory_order_relaxed);
            } else {
                if (next == nullptr) continue; // transient; try again
                // Attempt to swing head to the next node. On success we own
                // the old head and may reclaim it.
                if (head_.compare_exchange_weak(head, next, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    out = std::move(next->value);
                    // NOTE: temporarily disable immediate deletion of the old head
                    // to check for use-after-free bugs. This intentionally leaks
                    // memory and should only be used for debugging. If this
                    // removes the crash, replace deletion with a safe retire
                    // mechanism (hazard pointers/epoch) or call hp::retire.
                    // delete head; // disabled for debug
                    return true;
                }
            }
        }
    }

private:
    template<typename U>
    void push_impl(U&& v) {
        Node* node = new Node(std::forward<U>(v));
        while (true) {
            Node* tail = tail_.load(std::memory_order_acquire);
            Node* next = tail->next.load(std::memory_order_acquire);
            if (tail == tail_.load(std::memory_order_acquire)) {
                if (next == nullptr) {
                    // try to link new node at the end
                    if (tail->next.compare_exchange_weak(next, node, std::memory_order_release, std::memory_order_relaxed)) {
                        // try to swing tail to the inserted node (helping)
                        tail_.compare_exchange_weak(tail, node, std::memory_order_release, std::memory_order_relaxed);
                        return;
                    }
                } else {
                    // tail is behind, advance it
                    tail_.compare_exchange_weak(tail, next, std::memory_order_release, std::memory_order_relaxed);
                }
            }
        }
    }

    // head and tail point into a single-linked list of nodes; head points to
    // the dummy (node before first element). Both are atomic so multiple
    // producers/consumers can operate concurrently.
    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
};
