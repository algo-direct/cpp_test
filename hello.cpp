#include <iostream>


int main() {
    auto x = new int[40];
    for (int i = 0; i < 40; ++i) {
        x[i] = i;
    }
    for (int i = 0; i < 40; ++i) {
        std::cout << x[i] << " ";
    }
    delete[] x;
    return 0;
}

