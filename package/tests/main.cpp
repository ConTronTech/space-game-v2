#include "tests/test.h"

int main() {
    int ran = 0;
    for (auto& c : tst::cases()) {
        int before = tst::failures();
        c.fn();
        std::printf("%s %s\n", tst::failures() == before ? "ok  " : "FAIL", c.name);
        ran++;
    }
    std::printf("\n%d tests, %d failed checks\n", ran, tst::failures());
    return tst::failures() ? 1 : 0;
}
