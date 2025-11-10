// Model solution: reservoir sampling (reservoir size k)
#include <chrono>
#include <iostream>
#include <random>
#include <vector>

template<typename T>
std::vector<T> reservoir_sample(std::istream& in, size_t k) {
    std::vector<T> res;
    res.reserve(k);
    size_t n = 0;
    T val;
    std::mt19937_64 rng(123456);
    while (in >> val) {
        ++n;
        if (res.size() < k) res.push_back(val);
        else {
            std::uniform_int_distribution<size_t> dist(0, n-1);
            size_t idx = dist(rng);
            if (idx < k) res[idx] = val;
        }
    }
    return res;
}

// Simple test: deterministic stream from 1..N, sample k, ensure size k or less
#include <sstream>
int main() {
    const int N = 1000, K = 10;
    std::ostringstream oss;
    for (int i = 1; i <= N; ++i) {
        oss << i << ' ';
    }
    std::istringstream iss(oss.str());
    auto res = reservoir_sample<int>(iss, K);
    if (res.size() != (size_t)K) { std::cerr << "reservoir size mismatch\n"; return 2; }
    std::cout << "reservoir_sampling: PASS size=" << res.size() << "\n";
    return 0;
}
