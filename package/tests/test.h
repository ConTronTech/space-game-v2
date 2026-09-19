#pragma once
// Tiny test framework, no dependencies.
//   TEST(name) { CHECK(cond); CHECK_EQ(a, b); }
// Every tests/*.cpp file is compiled into `make test`. Add a file, add tests, done.
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace tst {
struct Case { const char* name; std::function<void()> fn; };
inline std::vector<Case>& cases() { static std::vector<Case> c; return c; }
inline int& failures() { static int f = 0; return f; }
struct Reg { Reg(const char* n, std::function<void()> f) { cases().push_back({n, std::move(f)}); } };
template <class A, class B>
void checkEq(const A& a, const B& b, const char* ea, const char* eb, const char* file, int line) {
    if (a == b) return;
    std::ostringstream ss;
    ss << "  FAIL " << file << ":" << line << "  " << ea << " == " << eb << "  (" << a << " vs " << b << ")\n";
    std::fputs(ss.str().c_str(), stderr);
    failures()++;
}
} // namespace tst

#define TEST(name) \
    static void test_##name(); \
    static tst::Reg reg_##name(#name, test_##name); \
    static void test_##name()
#define CHECK(cond) \
    do { if (!(cond)) { std::fprintf(stderr, "  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); tst::failures()++; } } while (0)
#define CHECK_EQ(a, b) tst::checkEq((a), (b), #a, #b, __FILE__, __LINE__)
