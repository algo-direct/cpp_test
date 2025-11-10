// tiny test: virtual + constexpr behavior under C++20
#include <iostream>

struct Base {
    virtual constexpr int f() const { return 1; }
    virtual ~Base() = default;
};

struct Derived : Base {
    constexpr int f() const override { return 2; }
};

int main() {
    constexpr Derived d;
    static_assert(d.f() == 2, "constexpr virtual call in Derived should work if allowed");
    const Base& b = d;
    std::cout << b.f() << "\n";
    return 0;
}
