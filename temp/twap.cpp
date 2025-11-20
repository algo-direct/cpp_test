// C++23: Incremental TWAP implementations (anchored & sliding-window)
// - Uses integer ticks for price (e.g., ticks_per_unit = 10000 for 4 decimals)
// - Uses ms timestamps (int64_t). Single-writer-per-symbol assumption.
// - Accumulates price * time using __int128 to prevent overflow.
//
// Build (example):
//   g++ -std=c++23 twap_examples.cpp -O2 -pthread -o twap_examples
//
// Notes:
// - TWAP here assumes price is piecewise-constant between trades:
//     when trade at t0 with price p0 and next trade at t1 with price p1,
//     price p0 is assumed to hold over [t0, t1) and contributes p0*(t1-t0).
// - If you use sampling (fixed-interval samples), TWAP is the average of sampled prices.

#include <cstdint>
#include <deque>
#include <optional>
#include <chrono>
#include <format>
#include <iostream>
#include <cassert>

using i64 = int64_t;
using i128 = __int128_t;

// Helper: current epoch ms
inline i64 now_ms() {
    using namespace std::chrono;
    return std::chrono::duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// Convert ticks -> price double (for display)
inline double ticks_to_price(i64 ticks, i64 ticks_per_unit) {
    return double(ticks) / double(ticks_per_unit);
}

// -----------------------------
// Anchored / cumulative TWAP
// -----------------------------
class CumulativeTWAP {
public:
    explicit CumulativeTWAP(i64 ticks_per_unit = 10000)
        : ticks_per_unit_(ticks_per_unit),
          price_time_accum_(0), total_time_ms_(0),
          have_last_(false), last_price_ticks_(0), last_ts_ms_(0) {}

    // Provide an initial anchor (e.g., session open price at ts)
    void set_anchor(i64 ts_ms, i64 price_ticks) {
        last_ts_ms_ = ts_ms;
        last_price_ticks_ = price_ticks;
        have_last_ = true;
    }

    // Add a trade (event-time timestamp in ms)
    // If this is the first trade and no anchor, we simply set last price/time
    // and won't accumulate until the next trade (or session close).
    void on_trade(i64 ts_ms, i64 price_ticks) {
        if (ts_ms < 0) return;
        if (!have_last_) {
            last_ts_ms_ = ts_ms;
            last_price_ticks_ = price_ticks;
            have_last_ = true;
            return;
        }
        // compute duration the last_price held
        i64 duration = ts_ms - last_ts_ms_;
        if (duration > 0) {
            price_time_accum_ += (i128)last_price_ticks_ * (i128)duration;
            total_time_ms_ += duration;
        }
        // update last
        last_ts_ms_ = ts_ms;
        last_price_ticks_ = price_ticks;
    }

    // If the session ends (e.g., market close) and the last price persists until end_ts,
    // call close_session(end_ts) to include the last active interval [last_ts, end_ts).
    void close_session(i64 end_ts_ms) {
        if (!have_last_) return;
        i64 duration = end_ts_ms - last_ts_ms_;
        if (duration > 0) {
            price_time_accum_ += (i128)last_price_ticks_ * (i128)duration;
            total_time_ms_ += duration;
        }
        // Optionally reset last anchor state (we choose to mark as no-last so
        // new session starts clean)
        have_last_ = false;
    }

    // Reset all aggregates (use at session boundary if desired)
    void reset() {
        price_time_accum_ = 0;
        total_time_ms_ = 0;
        have_last_ = false;
        last_price_ticks_ = 0;
        last_ts_ms_ = 0;
    }

    // Return TWAP as ticks (price * ticks_per_unit), nullopt if no time accumulated
    std::optional<i64> get_twap_ticks() const {
        if (total_time_ms_ == 0) return std::nullopt;
        i64 twap_ticks = (i64)(price_time_accum_ / (i128)total_time_ms_);
        return twap_ticks;
    }

    std::optional<double> get_twap_price() const {
        auto t = get_twap_ticks();
        if (!t) return std::nullopt;
        return ticks_to_price(*t, ticks_per_unit_);
    }

    // For checkpointing: expose accumulators (price_time_accum and total_time_ms)
    i128 price_time_accum() const { return price_time_accum_; }
    i64 total_time_ms() const { return total_time_ms_; }

private:
    const i64 ticks_per_unit_;
    i128 price_time_accum_;   // sum(price_ticks * duration_ms)
    i64 total_time_ms_;       // sum(duration_ms)
    // last seen trade anchor (price holds from last_ts until next event)
    bool have_last_;
    i64 last_price_ticks_;
    i64 last_ts_ms_;
};


// -----------------------------
// Sliding-window TWAP (time window T ms)
// -----------------------------
class SlidingWindowTWAP {
public:
    struct Segment {
        i64 start_ms;      // inclusive
        i64 end_ms;        // exclusive
        i64 price_ticks;   // price during [start_ms, end_ms)
    };

    explicit SlidingWindowTWAP(i64 window_ms, i64 ticks_per_unit = 10000)
        : window_ms_(window_ms), ticks_per_unit_(ticks_per_unit),
          price_time_accum_(0), total_time_ms_(0),
          have_last_(false), last_price_ticks_(0), last_ts_ms_(0) {}

    // Anchor to start (optional)
    void set_anchor(i64 ts_ms, i64 price_ticks) {
        last_ts_ms_ = ts_ms;
        last_price_ticks_ = price_ticks;
        have_last_ = true;
    }

    // Add a trade at timestamp ts_ms with price_ticks
    // This creates a segment [last_ts_ms_, ts_ms) with last_price_ticks_
    void on_trade(i64 ts_ms, i64 price_ticks) {
        if (ts_ms < 0) return;
        if (!have_last_) {
            last_ts_ms_ = ts_ms;
            last_price_ticks_ = price_ticks;
            have_last_ = true;
            return;
        }

        i64 seg_start = last_ts_ms_;
        i64 seg_end = ts_ms;
        if (seg_end > seg_start) {
            add_segment({seg_start, seg_end, last_price_ticks_});
        }
        // update last anchor
        last_ts_ms_ = ts_ms;
        last_price_ticks_ = price_ticks;

        // Evict older segments outside window relative to current ts_ms
        evict_old(ts_ms);
    }

    // Close session to include final segment until end_ts (optional)
    void close_session(i64 end_ts_ms) {
        if (!have_last_) return;
        if (end_ts_ms > last_ts_ms_) {
            add_segment({last_ts_ms_, end_ts_ms, last_price_ticks_});
            // do not update last_ts so caller may choose to reset
        }
    }

    // Sliding-window TWAP (current window defined as [now - window_ms_, now))
    std::optional<i64> get_twap_ticks(i64 now_ms) {
        evict_old(now_ms);
        if (total_time_ms_ == 0) return std::nullopt;
        return (i64)(price_time_accum_ / (i128)total_time_ms_);
    }

    std::optional<double> get_twap_price(i64 now_ms) {
        auto t = get_twap_ticks(now_ms);
        if (!t) return std::nullopt;
        return ticks_to_price(*t, ticks_per_unit_);
    }

    // Reset entire state
    void reset() {
        segments_.clear();
        price_time_accum_ = 0;
        total_time_ms_ = 0;
        have_last_ = false;
        last_price_ticks_ = 0;
        last_ts_ms_ = 0;
    }

private:
    // Add a full segment (no trimming) - internal helper
    void add_segment(const Segment &seg) {
        // Only positive-length segments
        if (seg.end_ms <= seg.start_ms) return;
        segments_.push_back(seg);
        i64 duration = seg.end_ms - seg.start_ms;
        price_time_accum_ += (i128)seg.price_ticks * (i128)duration;
        total_time_ms_ += duration;
    }

    // Evict or trim segments older than cutoff = now_ms - window_ms_
    void evict_old(i64 now_ms) {
        i64 cutoff = now_ms - window_ms_;
        // No eviction needed if cutoff <= earliest start
        while (!segments_.empty()) {
            Segment &front = segments_.front();
            if (front.end_ms <= cutoff) {
                // whole segment is out of window
                i64 dur = front.end_ms - front.start_ms;
                price_time_accum_ -= (i128)front.price_ticks * (i128)dur;
                total_time_ms_ -= dur;
                segments_.pop_front();
            } else if (front.start_ms < cutoff && front.end_ms > cutoff) {
                // partial overlap: trim front segment to [cutoff, end)
                i64 removed = cutoff - front.start_ms; // amount to remove
                price_time_accum_ -= (i128)front.price_ticks * (i128)removed;
                total_time_ms_ -= removed;
                front.start_ms = cutoff;
                break;
            } else {
                // front.start_ms >= cutoff -> nothing to evict
                break;
            }
        }
    }

    const i64 window_ms_;
    const i64 ticks_per_unit_;
    std::deque<Segment> segments_;
    i128 price_time_accum_;
    i64 total_time_ms_;

    // last seen trade anchor (price holds from last_ts until next trade)
    bool have_last_;
    i64 last_price_ticks_;
    i64 last_ts_ms_;
};


// -----------------------------
// Demo: small scenario
// -----------------------------
int main() {
    const i64 ticks_per_unit = 10000; // 4 decimals
    // Anchored TWAP example
    CumulativeTWAP ct(ticks_per_unit);

    // Anchor at t=1000 ms, price 100.00
    ct.set_anchor(1000, 1000000);
    // trade at 2000ms changes price to 101.00
    ct.on_trade(2000, 1010000);
    // trade at 3500ms changes price to 99.50
    ct.on_trade(3500, 995000);
    // close session at 5000ms (include last segment)
    ct.close_session(5000);

    if (auto tw = ct.get_twap_price()) {
        std::cout << std::format("Cumulative TWAP = {:.6f}\n", *tw);
    } else {
        std::cout << "Cumulative TWAP = none\n";
    }

    // Sliding-window TWAP (last 2000 ms)
    SlidingWindowTWAP sw(2000, ticks_per_unit);
    // set anchor at 1000ms price 100.00
    sw.set_anchor(1000, 1000000);
    sw.on_trade(2000, 1010000);  // segment [1000,2000): 100.00
    sw.on_trade(3500, 995000);   // segment [2000,3500): 101.00
    // now = 5000ms, window is [3000,5000)
    i64 now = 5000;
    // add a late trade at 4800 (changes price)
    sw.on_trade(4800, 1005000);  // segment [3500,4800): 99.50
    // close out to now
    sw.close_session(now);       // segment [4800,5000): 100.50

    if (auto tw = sw.get_twap_price(now)) {
        std::cout << std::format("Sliding-window TWAP ({} ms) = {:.6f}\n", 2000, *tw);
    } else {
        std::cout << "Sliding-window TWAP = none\n";
    }

    return 0;
}