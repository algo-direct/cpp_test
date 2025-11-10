// Starter: very small fixed-size slab allocator
// Improved slab allocator: support alignment, thread-safe option (mutex), and clearer API.
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <vector>

class Slab {
public:
    // elem_size: raw size in bytes for each element (will be rounded up for alignment)
    // count: number of elements
    // align: alignment for returned blocks
    Slab(size_t elem_size, size_t count, size_t align = alignof(std::max_align_t))
    : elem_size_(round_up(elem_size, align)), count_(count), free_list_(), m_(), raw_(nullptr) {
        if (elem_size_ == 0 || count_ == 0) throw std::invalid_argument("invalid slab params");
        size_t total = elem_size_ * count_;
        int rc = posix_memalign(&raw_, align, total);
        if (rc != 0 || raw_ == nullptr) throw std::bad_alloc();
        // memory is owned at raw_; we do not copy into a vector to preserve alignment
        free_list_.reserve(count_);
        for (size_t i = 0; i < count_; ++i) free_list_.push_back(i);
    }

    ~Slab() {
        if (raw_) free(raw_);
    }

    void* alloc() {
        std::lock_guard lock(m_);
        if (free_list_.empty()) return nullptr;
    size_t idx = free_list_.back(); free_list_.pop_back();
    return static_cast<char*>(raw_) + idx * elem_size_;
    }

    void free(void* p) {
        std::lock_guard lock(m_);
        uintptr_t base = reinterpret_cast<uintptr_t>(raw_);
        uintptr_t ptr = reinterpret_cast<uintptr_t>(p);
        assert(ptr >= base && ptr < base + elem_size_ * count_);
        size_t off = (ptr - base) / elem_size_;
        free_list_.push_back(off);
    }

    size_t available() const {
        std::lock_guard lock(m_);
        return free_list_.size();
    }

private:
    static size_t round_up(size_t v, size_t align) {
        return (v + align - 1) & ~(align - 1);
    }

    size_t elem_size_, count_;
    std::vector<size_t> free_list_;
    mutable std::mutex m_;
    void* raw_;
};

int main() {
    Slab s(64, 10, 64);
    void* a[10];
    for (int i=0;i<10;++i) { a[i]=s.alloc(); if(!a[i]) { std::cerr<<"alloc fail\n"; return 2; } }
    if (s.alloc()!=nullptr) { std::cerr<<"overflow fail\n"; return 3; }
    s.free(a[5]);
    void* p = s.alloc(); if (p==nullptr) { std::cerr<<"realloc fail\n"; return 4; }
    if (reinterpret_cast<uintptr_t>(p) % 64 != 0) { std::cerr<<"alignment fail\n"; return 5; }
    std::cout<<"slab_allocator: PASS\n";
    return 0;
}
