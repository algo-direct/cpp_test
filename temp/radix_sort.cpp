// Starter: LSD radix sort for uint32_t
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

void radix_sort_32(std::vector<uint32_t>& a) {
    const size_t n = a.size();
    std::vector<uint32_t> buf(n);
    const int B = 8;
    const int R = 1 << B;
    std::vector<size_t> cnt(R);
    for (int pass = 0; pass < 4; ++pass) {
        std::fill(cnt.begin(), cnt.end(), 0);
        int shift = pass * B;
        for (size_t i = 0; i < n; ++i) cnt[(a[i] >> shift) & (R-1)]++;
        size_t sum = 0;
        for (int i = 0; i < R; ++i) { size_t t = cnt[i]; cnt[i] = sum; sum += t; }
        for (size_t i = 0; i < n; ++i) {
            buf[cnt[(a[i] >> shift) & (R-1)]++] = a[i];
        }
        a.swap(buf);
    }
}

int main(){
    std::vector<uint32_t> v = {3,1,4,1,5,9,2,6,5};
    radix_sort_32(v);
    for (size_t i=1;i<v.size();++i) if (v[i-1]>v[i]) { std::cerr<<"radix fail\n"; return 2; }
    std::cout<<"radix_sort: PASS\n";
    return 0;
}
