// Improved: Thread-safe LRU cache with capacity and move-aware interface.
// This implementation uses a mutex for correctness; a sharded design or lock-free
// approach can be used for high-throughput low-latency use cases.

#include <list>
#include <iostream>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

template<typename K, typename V>
class LRUCache {
public:
    explicit LRUCache(size_t cap) : cap_(cap) {
        if (cap_ == 0) cap_ = 1;
    }

    // get returns optional<V> to avoid extra out parameters
    std::optional<V> get(const K& k) {
        std::unique_lock lock(m_);
        auto it = map_.find(k);
        if (it == map_.end()) return std::nullopt;
        // move the entry to front
        items_.splice(items_.begin(), items_, it->second);
        return it->second->second;
    }

    void put(const K& k, V v) {
        std::unique_lock lock(m_);
        auto it = map_.find(k);
        if (it != map_.end()) {
            it->second->second = std::move(v);
            items_.splice(items_.begin(), items_, it->second);
            return;
        }
        items_.emplace_front(k, std::move(v));
        map_[k] = items_.begin();
        if (map_.size() > cap_) {
            auto last = items_.end(); --last;
            map_.erase(last->first);
            items_.pop_back();
        }
    }

    size_t size() const {
        std::shared_lock lock(m_);
        return map_.size();
    }

private:
    size_t cap_;
    mutable std::shared_mutex m_; // shared for size() but exclusive for mutations
    std::list<std::pair<K,V>> items_;
    std::unordered_map<K, typename std::list<std::pair<K,V>>::iterator> map_;
};

// Simple tests
int main() {
    LRUCache<int,int> c(2);
    c.put(1,1);
    c.put(2,2);
    auto r1 = c.get(1);
    if (!r1 || *r1 != 1) { std::cerr << "lru get fail\n"; return 2; }
    c.put(3,3);
    auto r2 = c.get(2);
    if (r2) { std::cerr << "lru evict fail\n"; return 3; }
    std::cout << "lru_cache: PASS\n";
    return 0;
}
