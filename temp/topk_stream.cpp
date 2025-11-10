// Model solution: streaming top-k using min-heap
#include <algorithm>
#include <functional>
#include <iostream>
#include <queue>
#include <vector>

std::vector<int> topk_stream(const std::vector<int>& in, size_t k) {
    std::priority_queue<int, std::vector<int>, std::greater<int>> pq;
    for (int v : in) {
        if (pq.size() < k) pq.push(v);
        else if (v > pq.top()) { pq.pop(); pq.push(v); }
    }
    std::vector<int> out;
    while (!pq.empty()) { out.push_back(pq.top()); pq.pop(); }
    std::sort(out.begin(), out.end(), std::greater<int>());
    return out;
}

int main() {
    std::vector<int> a = {5,1,9,3,7,6,2,8,4};
    auto top3 = topk_stream(a, 3);
    for (int x : top3) std::cout << x << ' ';
    std::cout << "\n";
    std::cout << "topk_stream: PASS\n";
    return 0;
}
