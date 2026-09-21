#include <chrono>
#include <cmath>
#include <numeric>
#include "gameplay/mining/mining_rules.h"
#include "tests/test.h"

namespace {
using gameplay::Vec3d;
bool close(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol * std::max(1.0, std::fabs(b)); }
double len(const Vec3d& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
} // namespace

TEST(mining_yield_follows_the_radius_squared) {
    CHECK_EQ(gameplay::totalYield(9.0f, 0.5f), 41);                      // "a radius-9 rock: about 40 ore units"
    CHECK_EQ(gameplay::totalYield(3.0f, 0.5f), 5);                       // a radius-3 rock: a handful
    CHECK(gameplay::totalYield(60.0f, 0.5f) > gameplay::totalYield(30.0f, 0.5f) * 3);
    CHECK_EQ(gameplay::totalYield(0.5f, 0.5f), 1);                       // never less than 1
    CHECK_EQ(gameplay::totalYield(0.0f, 0.0f), 1);
    CHECK_EQ(gameplay::totalYield(9.0f, 1.0f), 81);                      // the scale is linear
}

TEST(mining_chunks_split_the_total_exactly) {
    for (int total : {1, 2, 5, 41, 100, 4500}) {
        int n = gameplay::chunkCount(total, 1.0f);
        CHECK(n >= 1 && n <= std::min(total, 16));
        std::vector<int> parts;
        gameplay::splitAmount(total, n, parts);
        CHECK_EQ((int)parts.size(), n);
        CHECK_EQ(std::accumulate(parts.begin(), parts.end(), 0), total);  // nothing lost or invented
        for (int p : parts) CHECK(p >= 1);
        CHECK(parts.front() - parts.back() <= 1);                         // even split
    }
    CHECK_EQ(gameplay::chunkCount(0, 1.0f), 0); CHECK_EQ(gameplay::chunkCount(-5, 1.0f), 0);
    CHECK_EQ(gameplay::chunkCount(41, 0.0f), 1);                          // scale 0 still gives one chunk
    CHECK(gameplay::chunkCount(41, 3.0f) <= 16);
    std::vector<int> none; gameplay::splitAmount(0, 3, none); CHECK(none.empty()); gameplay::splitAmount(5, 0, none); CHECK(none.empty());
    gameplay::splitAmount(3, 10, none); CHECK_EQ((int)none.size(), 3);    // more parts than units: one unit each
}

TEST(mining_chunk_velocity_is_outward_seeded_and_bounded) {
    gameplay::Rng rng(4);
    for (int i = 0; i < 300; i++) {
        Vec3d v = gameplay::chunkVelocity(rng, 2.0f, 10.0f);
        CHECK(len(v) >= 2.0 - 1e-4 && len(v) <= 10.0 + 1e-4);
    }
    gameplay::Rng a(9), b(9);
    Vec3d va = gameplay::chunkVelocity(a, 2.0f, 10.0f), vb = gameplay::chunkVelocity(b, 2.0f, 10.0f);
    CHECK(close(va.x, vb.x) && close(va.y, vb.y) && close(va.z, vb.z));   // deterministic with the same seed
    Vec3d moving = gameplay::chunkVelocity(a, 0.0f, 0.0f, {5, 0, 0});     // the asteroid's own velocity is added
    CHECK(close(moving.x, 5.0)); CHECK(close(moving.y, 0.0));
}

TEST(mining_scoop_needs_closeness_and_a_slow_relative_speed) {
    CHECK(gameplay::canScoop(5.0, 10.0, 12.0, 40.0));                     // close and slow: collected
    CHECK(gameplay::canScoop(12.0, 40.0, 12.0, 40.0));                    // exactly at the limits
    CHECK(!gameplay::canScoop(12.5, 10.0, 12.0, 40.0));                   // too far
    CHECK(!gameplay::canScoop(5.0, 41.0, 12.0, 40.0));                    // too fast: a fast fly-through collects nothing
    CHECK(!gameplay::canScoop(50.0, 500.0, 12.0, 40.0));
}

TEST(mining_pool_spawn_evicts_the_oldest_and_never_overflows) {
    gameplay::ChunkPool pool; pool.init(3);
    int a = pool.spawn({0, 0, 0}, {0, 0, 0}, 1, 1);
    CHECK_EQ(a, 0);
    gameplay::advanceChunks(pool, 1.0f, 300.0f, 3000.0f, {0, 0, 0});      // chunk 0 is now the oldest
    pool.spawn({1, 0, 0}, {0, 0, 0}, 2, 1); pool.spawn({2, 0, 0}, {0, 0, 0}, 3, 1);
    CHECK_EQ(pool.n, 3);
    int slot = pool.spawn({3, 0, 0}, {0, 0, 0}, 4, 2);                    // full: the oldest (slot 0, age 1.0) is replaced
    CHECK_EQ(slot, 0); CHECK_EQ(pool.n, 3); CHECK_EQ(pool.evicted, 1); CHECK_EQ(pool.amount[0], 4); CHECK(close(pool.age[0], 0.0));
    CHECK_EQ(pool.spawn({0, 0, 0}, {0, 0, 0}, 0, 1), -1);                 // a zero amount is not a chunk
    gameplay::ChunkPool none; none.init(0); CHECK_EQ(none.spawn({0, 0, 0}, {0, 0, 0}, 5, 1), -1);   // mining.max_chunks 0: nothing, no crash
    CHECK_EQ(pool.n, 3);
}

TEST(mining_chunks_drift_newtonian_and_despawn_by_age_and_range) {
    gameplay::ChunkPool pool; pool.init(10);
    pool.spawn({100, 0, 0}, {3, 0, 0}, 5, 1);
    for (int i = 0; i < 600; i++) gameplay::advanceChunks(pool, 1.0f / 60, 300.0f, 3000.0f, {0, 0, 0});   // 10 s
    CHECK(close(pool.px[0], 130.0, 1e-4));                                // moved 3 m/s x 10 s, no drag
    CHECK(close(pool.vx[0], 3.0));                                        // and still at 3 m/s: it drifts forever
    // age: gone after the lifetime
    gameplay::ChunkPool old; old.init(4); old.spawn({0, 0, 0}, {0, 0, 0}, 1, 1);
    gameplay::advanceChunks(old, 299.0f, 300.0f, 3000.0f, {0, 0, 0}); CHECK_EQ(old.n, 1);
    gameplay::advanceChunks(old, 2.0f, 300.0f, 3000.0f, {0, 0, 0}); CHECK_EQ(old.n, 0);
    // range: a chunk far from the ship goes; the near one stays
    gameplay::ChunkPool far; far.init(4);
    far.spawn({5000, 0, 0}, {0, 0, 0}, 1, 1); far.spawn({100, 0, 0}, {0, 0, 0}, 2, 1);
    gameplay::advanceChunks(far, 0.1f, 300.0f, 3000.0f, {0, 0, 0});
    CHECK_EQ(far.n, 1); CHECK_EQ(far.amount[0], 2);
    gameplay::advanceChunks(far, 0.0f, 0.0f, 0.0f, {0, 0, 0}); gameplay::advanceChunks(far, -1.0f, 0.0f, 0.0f, {0, 0, 0}); CHECK_EQ(far.n, 1);   // dt <= 0: no change
    // swap-remove keeps the survivors: spawn 4, remove the 2nd by range
    gameplay::ChunkPool s; s.init(8);
    for (int i = 0; i < 4; i++) s.spawn({i == 1 ? 9000.0 : 10.0 * i, 0, 0}, {0, 0, 0}, i + 1, 1);
    gameplay::advanceChunks(s, 0.1f, 300.0f, 3000.0f, {0, 0, 0});
    CHECK_EQ(s.n, 3);
    int sum = 0; for (int i = 0; i < s.n; i++) sum += s.amount[i]; CHECK_EQ(sum, 1 + 3 + 4);
}

TEST(mining_cpu_cost_of_96_chunks_is_small) {
    gameplay::ChunkPool pool; pool.init(96);
    gameplay::Rng rng(1);
    for (int i = 0; i < 96; i++) pool.spawn({i * 3.0, 0, 0}, gameplay::chunkVelocity(rng, 2, 10), 5, 1);
    auto t0 = std::chrono::steady_clock::now();
    long scoopChecks = 0;
    for (int step = 0; step < 600; step++) {
        gameplay::advanceChunks(pool, 1.0f / 60, 300.0f, 3000.0f, {0, 0, 0});
        for (int i = 0; i < pool.n; i++) { double d = std::sqrt(pool.px[i] * pool.px[i] + pool.py[i] * pool.py[i] + pool.pz[i] * pool.pz[i]); if (gameplay::canScoop(d, 5.0, 12.0, 40.0)) scoopChecks++; }
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 600.0;
    std::fprintf(stderr, "  [mining] 96 chunks: advance + scoop test = %.4f ms per fixed step (%ld scoop hits)\n", ms, scoopChecks);
    CHECK(ms < 1.0);
}
