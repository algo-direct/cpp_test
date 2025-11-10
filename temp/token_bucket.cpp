// Model solution: token bucket rate limiter
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

class TokenBucket {
public:
    TokenBucket(double rate_per_sec, double burst)
    : rate_(rate_per_sec), capacity_(burst), tokens_(burst), last_(std::chrono::steady_clock::now()) {}

    // try to consume n tokens; returns true if allowed
    bool try_consume(double n = 1.0) {
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double> d = now - last_;
        double add = d.count() * rate_;
        last_ = now;
        tokens_ = std::min(capacity_, tokens_ + add);
        if (tokens_ >= n) { tokens_ -= n; return true; }
        return false;
    }

private:
    const double rate_;
    const double capacity_;
    double tokens_;
    std::chrono::steady_clock::time_point last_;
};

int main() {
    TokenBucket tb(1000.0, 200.0); // 1000 tokens/sec, burst 200
    int allowed = 0;
    for (int i = 0; i < 500; ++i) {
        if (tb.try_consume()) ++allowed;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    std::cout << "token_bucket: allowed=" << allowed << "\n";
    return 0;
}
