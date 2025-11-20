// C++23 reference: multi-asset basket executor with per-venue rate-limits.
// Build: g++ -std=c++23 multi_asset_basket_executor.cpp -O2 -pthread -o basket_exec
//
// Notes:
// - Replace send_to_venue() simulation with your venue API (FIX/REST/WebSocket).
// - Integrate persistence, risk checks and real error codes as required.
// - Single-writer per coordinator in this example; adapt to your threading model.

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <format>

using namespace std::chrono;
using ms = std::chrono::milliseconds;
using steady = std::chrono::steady_clock;
using i64 = long long;

// -------------------- Utilities --------------------
static inline i64 now_ms() {
    return duration_cast<milliseconds>(steady::now().time_since_epoch()).count();
}

struct Order {
    std::string client_order_id;
    std::string symbol;
    int64_t qty;
    double price;             // For limit orders (or 0 for market)
    bool is_hedge_leg = false; // priority marker
    int attempts = 0;
    i64 next_eligible_ts = 0; // for backoff
};

// Simulated send result
enum class SendResult { OK, TEMP_REJECT /*e.g. 429/503*/, PERM_REJECT /*validation*/ };

// -------------------- Token Bucket --------------------
class TokenBucket {
public:
    TokenBucket(double rate_per_sec, double burst)
        : rate_per_sec_(rate_per_sec),
          capacity_(burst),
          tokens_(burst),
          last_ts_(steady::now()) {}

    // Try consume `tokens` amount; returns true if consumed.
    bool try_consume(double tokens = 1.0) {
        std::lock_guard lock(mu_);
        refill_locked();
        if (tokens_ + 1e-12 >= tokens) {
            tokens_ -= tokens;
            return true;
        }
        return false;
    }

    // How many tokens available (approx)
    double available() {
        std::lock_guard lock(mu_);
        refill_locked();
        return tokens_;
    }

private:
    void refill_locked() {
        auto now = steady::now();
        double dt = duration_cast<duration<double>>(now - last_ts_).count();
        if (dt <= 0) return;
        tokens_ = std::min(capacity_, tokens_ + dt * rate_per_sec_);
        last_ts_ = now;
    }

    const double rate_per_sec_;
    const double capacity_;
    double tokens_;
    steady::time_point last_ts_;
    std::mutex mu_;
};

// -------------------- Simple exponential backoff --------------------
i64 backoff_delay_ms(int attempt, int base_ms = 100, int cap_ms = 5000) {
    // attempt starts at 1
    static std::mt19937_64 rng((uint64_t)std::random_device{}());
    static std::uniform_real_distribution<double> jitter(0.8, 1.2);
    int64_t delay = std::min<int64_t>(cap_ms, (1LL << std::min(20, attempt)) * base_ms);
    double j = jitter(rng);
    return (i64)(delay * j);
}

// -------------------- Venue Dispatcher --------------------
struct VenueConfig {
    std::string name;
    double orders_per_sec = 10.0;
    double msgs_per_sec = 50.0;
    double burst_orders = 5.0;
    int max_concurrent_requests = 4;
};

class VenueDispatcher {
public:
    VenueDispatcher(VenueConfig cfg, std::function<SendResult(const Order&)> send_fn)
        : cfg_(std::move(cfg)),
          order_bucket_(cfg_.orders_per_sec, cfg_.burst_orders),
          msg_bucket_(cfg_.msgs_per_sec, cfg_.msgs_per_sec), // msg burst == msgs/sec by default
          send_fn_(std::move(send_fn)),
          concurrent_requests_(0),
          stopped_(false) {}

    // enqueue a child order for this venue
    void enqueue(std::shared_ptr<Order> order) {
        std::lock_guard lock(mu_);
        queue_.push_back(order);
        cv_.notify_one();
    }

    // Scheduler loop; runs in its own thread
    void run() {
        std::unique_lock lock(mu_);
        while (!stopped_) {
            if (queue_.empty()) {
                cv_.wait_for(lock, ms(50));
                continue;
            }
            // find the next eligible order (respect next_eligible_ts)
            auto it = std::find_if(queue_.begin(), queue_.end(),
                [](const std::shared_ptr<Order>& o){ return o->next_eligible_ts <= now_ms(); });
            if (it == queue_.end()) {
                // nothing eligible yet; sleep until earliest next_eligible_ts
                i64 earliest = queue_.front()->next_eligible_ts;
                for (auto &o : queue_) earliest = std::min(earliest, o->next_eligible_ts);
                i64 wait_ms = std::max<i64>(1, earliest - now_ms());
                cv_.wait_for(lock, ms(wait_ms));
                continue;
            }
            // Respect concurrency and token buckets
            if (concurrent_requests_ >= cfg_.max_concurrent_requests) {
                // Wait until some request finishes or short sleep to avoid busy-loop
                cv_.wait_for(lock, ms(1));
                continue;
            }
            if (!order_bucket_.try_consume(1.0) || !msg_bucket_.try_consume(1.0)) {
                // tokens unavailable; wait a bit
                cv_.wait_for(lock, ms(1));
                continue;
            }
            // Pop the order and dispatch asynchronously
            auto order = *it;
            queue_.erase(it);
            ++concurrent_requests_;
            // unlock while sending (send may be async in real impl)
            lock.unlock();
            std::thread(&VenueDispatcher::dispatch_order, this, order).detach();
            lock.lock();
        }
    }

    void stop() {
        std::lock_guard lock(mu_);
        stopped_ = true;
        cv_.notify_all();
    }

    // For monitoring
    size_t queued_size() { std::lock_guard l(mu_); return queue_.size(); }
    int concurrent_requests() const { return concurrent_requests_.load(); }

private:
    void dispatch_order(std::shared_ptr<Order> order) {
        // simulate network call to venue API via provided send_fn_
        SendResult r = send_fn_(*order);
        // Post-send handling
        if (r == SendResult::OK) {
            // completed
            std::cout << std::format("[{}] Sent {} {}@{} OK\n", cfg_.name, order->client_order_id, order->symbol, order->price);
        } else if (r == SendResult::TEMP_REJECT) {
            // schedule retry with exponential backoff
            order->attempts += 1;
            i64 delay = backoff_delay_ms(order->attempts);
            order->next_eligible_ts = now_ms() + delay;
            // re-enqueue
            std::lock_guard lock(mu_);
            queue_.push_back(order);
            cv_.notify_one();
            std::cout << std::format("[{}] Temp reject {} -> retry in {}ms\n", cfg_.name, order->client_order_id, delay);
        } else {
            // permanent reject -> log and drop or escalate
            std::cout << std::format("[{}] Perm reject {} -> dropping/notify\n", cfg_.name, order->client_order_id);
        }
        // release concurrency slot
        --concurrent_requests_;
        // notify scheduler thread
        cv_.notify_one();
    }

    VenueConfig cfg_;
    TokenBucket order_bucket_;
    TokenBucket msg_bucket_;
    std::deque<std::shared_ptr<Order>> queue_;
    std::function<SendResult(const Order&)> send_fn_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::atomic<int> concurrent_requests_;
    bool stopped_;
};

// -------------------- Basket Executor (Coordinator) --------------------
class BasketExecutor {
public:
    BasketExecutor() = default;

    void add_venue(const VenueConfig& cfg, std::function<SendResult(const Order&)> send_fn) {
        std::lock_guard lock(mu_);
        auto vd = std::make_shared<VenueDispatcher>(cfg, send_fn);
        venues_[cfg.name] = vd;
        threads_.emplace_back([vd]{ vd->run(); });
    }

    void stop_all() {
        for (auto &kv : venues_) kv.second->stop();
        for (auto &t : threads_) if (t.joinable()) t.join();
    }

    // Plan and enqueue orders for a basket: map<venue, vector<orders>>
    void submit_basket(const std::unordered_map<std::string, std::vector<Order>>& plan) {
        for (auto &kv : plan) {
            const auto &venue = kv.first;
            auto it = venues_.find(venue);
            if (it == venues_.end()) {
                std::cerr << "Unknown venue: " << venue << "\n";
                continue;
            }
            for (auto o : kv.second) {
                // set initial COID, eligibility
                auto p = std::make_shared<Order>(o);
                p->client_order_id = std::format("coid-{}-{}", o.symbol, ++coid_seq_);
                p->next_eligible_ts = now_ms();
                it->second->enqueue(p);
            }
        }
    }

private:
    std::mutex mu_;
    std::map<std::string, std::shared_ptr<VenueDispatcher>> venues_;
    std::vector<std::thread> threads_;
    std::atomic<uint64_t> coid_seq_{0};
};

// -------------------- Simulation send function --------------------
// In production, implement real API calls with proper sync/async semantics.
SendResult simulated_send(const Order& o) {
    // random transient failure simulation
    static thread_local std::mt19937_64 rng((uint64_t)steady::now().time_since_epoch().count());
    std::uniform_real_distribution<double> u(0.0, 1.0);
    double chance = u(rng);
    if (chance < 0.85) return SendResult::OK;
    if (chance < 0.95) return SendResult::TEMP_REJECT;
    return SendResult::PERM_REJECT;
}

// -------------------- Example usage --------------------
int main() {
    BasketExecutor exec;

    VenueConfig vA{"EX-A", 50.0, 200.0, 10.0, 8}; // 50 orders/sec, 200 msgs/sec, burst 10, 8 concurrent
    VenueConfig vB{"EX-B", 20.0, 100.0, 5.0, 4};
    exec.add_venue(vA, simulated_send);
    exec.add_venue(vB, simulated_send);

    // Build a basket of 100 symbols, plan splitting to venues
    std::unordered_map<std::string, std::vector<Order>> plan;
    for (int i = 0; i < 100; ++i) {
        Order o;
        o.symbol = std::format("SYM{:03}", i);
        o.qty = 100;
        o.price = 100.0 + i * 0.01;
        // simple round-robin venue assignment
        std::string venue = (i % 2 == 0) ? "EX-A" : "EX-B";
        plan[venue].push_back(o);
    }

    exec.submit_basket(plan);

    // Let it run for a while
    std::this_thread::sleep_for(std::chrono::seconds(10));

    exec.stop_all();
    return 0;
}