#include <iostream>
struct Base { virtual constexpr int f() const { return 1; } virtual ~Base()=default; };
struct Derived: Base { constexpr int f() const override { return 2; } };
int main(){
    constexpr Derived d;
    static_assert(d.f()==2); // should be fine
    constexpr const Base& br = d; // bind to derived
    static_assert(br.f()==2); // expect error: virtual call in constant expression
    (void)br;
    return 0;
}
