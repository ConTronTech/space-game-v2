#include <cmath>
#include "core/benchmark/bench_stats.h"
#include "tests/test.h"

TEST(bench_empty_input_is_all_zero) {
    auto s = bench::compute({});
    CHECK_EQ(s.frames, 0);
    CHECK_EQ(s.avgFps, 0.0);
    CHECK_EQ(s.lowFps, 0.0);
}

TEST(bench_steady_frames) {
    std::vector<double> ms(100, 10.0);                       // a perfect 100 fps
    auto s = bench::compute(ms);
    CHECK_EQ(s.frames, 100);
    CHECK(std::abs(s.avgMs - 10.0) < 1e-9);
    CHECK(std::abs(s.avgFps - 100.0) < 1e-6);
    CHECK(std::abs(s.p99Ms - 10.0) < 1e-9);
    CHECK(std::abs(s.lowFps - 100.0) < 1e-6);
    CHECK(std::abs(s.worstMs - 10.0) < 1e-9);
    CHECK(std::abs(s.seconds - 1.0) < 1e-9);
}

TEST(bench_one_stutter_hits_worst_and_percentile_only_when_it_is_in_the_top_percent) {
    std::vector<double> ms(200, 10.0);
    ms[57] = 250.0;                                          // one hitch in 200 frames = 0.5%: not in the worst 1%
    auto s = bench::compute(ms);
    CHECK(std::abs(s.worstMs - 250.0) < 1e-9);
    CHECK(std::abs(s.p99Ms - 10.0) < 1e-9);                  // the 99th percentile ignores a single outlier
    CHECK(s.avgMs > 10.0);                                   // ...but the average feels it

    ms[58] = 250.0; ms[59] = 250.0;                          // 3 hitches in 200 = 1.5%: now they define the 1% low
    s = bench::compute(ms);
    CHECK(std::abs(s.p99Ms - 250.0) < 1e-9);
    CHECK(std::abs(s.lowFps - 4.0) < 1e-6);
}

TEST(bench_single_sample_and_ordering_independence) {
    auto one = bench::compute({16.6});
    CHECK_EQ(one.frames, 1);
    CHECK(std::abs(one.p99Ms - 16.6) < 1e-9);
    auto a = bench::compute({5, 30, 10, 20, 15});
    auto b = bench::compute({30, 20, 15, 10, 5});
    CHECK(std::abs(a.p99Ms - b.p99Ms) < 1e-9);
    CHECK(std::abs(a.avgMs - 16.0) < 1e-9);
}
