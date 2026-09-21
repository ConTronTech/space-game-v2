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

// ---- capsules (mining.mode = capsule) ----
TEST(mining_capsule_flash_speeds_up_in_the_last_five_seconds_and_never_jumps) {
    using namespace gameplay;
    CHECK(close(flashHz(0, 30), kFlashHz)); CHECK(close(flashHz(24.9f, 30), kFlashHz));            // steady until 5 s before the end
    CHECK(flashHz(26, 30) > kFlashHz); CHECK(flashHz(28, 30) > flashHz(26, 30));                    // then faster and faster
    CHECK(close(flashHz(30, 30), kFlashHzFast)); CHECK(close(flashHz(35, 30), kFlashHzFast));       // capped at the fast rate
    // the phase (cycles) is continuous and increasing: the pulse never jumps when the speed-up starts
    float prev = flashCycles(0, 30);
    for (float t = 0.01f; t < 30.0f; t += 0.01f) {
        float c = flashCycles(t, 30);
        CHECK(c >= prev - 1e-6f); CHECK(c - prev < 0.06f);                                          // at most 6 Hz * 0.01 s = 0.06 cycles per step
        prev = c;
    }
    CHECK(close(flashCycles(10, 30), kFlashHz * 10, 1e-4));                                          // steady part: frequency x time
    // cycles over the last 5 s = mean frequency (1.5 and 6 -> 3.75) x 5 s
    CHECK(close(flashCycles(30, 30) - flashCycles(25, 30), 3.75f * 5.0f, 1e-3));
    for (float t = 0; t < 30; t += 0.37f) { float p = flashPulse(t, 30); CHECK(p >= -1e-6f && p <= 1.0f + 1e-6f); }
    // it really flashes: a 1.5 Hz flash has its bright and dark phase within one second
    float lo = 1, hi = 0; for (float t = 0; t < 1.0f; t += 0.01f) { float p = flashPulse(t, 30); lo = std::min(lo, p); hi = std::max(hi, p); }
    CHECK(lo < 0.05f && hi > 0.95f);
    // a very short life (the warning is longer than the life) is still continuous
    CHECK(flashCycles(2, 3) >= flashCycles(1, 3));
    CapsuleLook l = capsuleLook(0, 30, false); CHECK(!l.red); CHECK(l.size >= 0.8f - 1e-4f && l.size <= 1.4f + 1e-4f);
    CHECK(capsuleLook(5, 30, true).red);                                                             // blocked = red
}

TEST(mining_capsule_pool_capacity_and_eviction) {
    using namespace gameplay;
    CapsulePool p; p.init(3);
    p.spawn({0, 0, 0}, 5, 1, 30.0f); p.spawn({1, 0, 0}, 6, 1, 30.0f); p.spawn({2, 0, 0}, 7, 1, 30.0f);
    ageCapsules(p, 20.0f);                                                                           // all at 20 of 30 s
    p.age[1] = 25.0f;                                                                                // capsule 1 is the closest to expiring
    int slot = p.spawn({9, 0, 0}, 9, 2, 30.0f);                                                      // full: replaces the one about to expire (least loss)
    CHECK_EQ(slot, 1); CHECK_EQ(p.n, 3); CHECK_EQ(p.evicted, 1); CHECK_EQ(p.amount[1], 9); CHECK(close(p.age[1], 0.0));
    CHECK_EQ(p.spawn({0, 0, 0}, 0, 1, 30.0f), -1);                                                   // an empty capsule is not a capsule
    CapsulePool none; none.init(0); CHECK_EQ(none.spawn({0, 0, 0}, 5, 1, 30.0f), -1);               // mining.max_chunks 0: nothing, no crash
    CapsulePool q; q.init(4); q.spawn({0, 0, 0}, 5, 1, 30.0f, true); CHECK(q.blocked[0]);            // a capsule can be born blocked (instant mode remainder)
}

TEST(mining_capsule_expires_after_its_lifetime) {
    using namespace gameplay;
    CapsulePool p; p.init(4);
    p.spawn({0, 0, 0}, 5, 1, 30.0f); p.spawn({1, 0, 0}, 6, 1, 10.0f);
    ageCapsules(p, 9.9f); CHECK_EQ(p.n, 2);
    ageCapsules(p, 0.2f); CHECK_EQ(p.n, 1); CHECK_EQ(p.amount[0], 5);                               // the 10 s one is gone
    ageCapsules(p, 0.0f); ageCapsules(p, -3.0f); CHECK_EQ(p.n, 1);                                   // dt <= 0: no change
    ageCapsules(p, 20.0f); CHECK_EQ(p.n, 0);                                                         // the 30 s one expires too
}

TEST(mining_magnet_pulls_the_capsule_in_from_the_magnet_radius_and_leaves_far_ones_alone) {
    using namespace gameplay;
    CapsulePool p; p.init(4);
    p.spawn({100, 0, 0}, 5, 1, 30.0f);                                                               // 100 units away, inside the 120 magnet radius
    Vec3d ship{0, 0, 0};
    double first = 100, last = 100; int steps = 0;
    for (; steps < 60 * 10; steps++) {
        magnetStep(p, 0, ship, 1.0f / 60, 120.0f, 140.0f);
        double d = std::sqrt(p.px[0] * p.px[0] + p.py[0] * p.py[0] + p.pz[0] * p.pz[0]);
        CHECK(d <= last + 1e-6);                                                                     // monotonic approach: never backs off
        last = d;
        if (d <= 20.0) break;                                                                        // the scoop radius
    }
    CHECK(last <= 20.0); CHECK(steps < 60 * 3); CHECK(first > last);                                // arrives within 3 s, no need to match speed
    double speed = std::sqrt((double)p.vx[0] * p.vx[0] + (double)p.vy[0] * p.vy[0] + (double)p.vz[0] * p.vz[0]);
    CHECK(speed <= 140.0 + 1e-3);                                                                    // capped
    // outside the magnet radius: it just drifts (and slows), it is not pulled
    CapsulePool f; f.init(2); f.spawn({500, 0, 0}, 5, 1, 30.0f);
    for (int i = 0; i < 120; i++) magnetStep(f, 0, ship, 1.0f / 60, 120.0f, 140.0f);
    CHECK(close(f.px[0], 500.0)); CHECK(close(f.vx[0], 0.0));
    // a blocked capsule stays put even next to the ship (it flashes red and waits)
    CapsulePool b; b.init(2); b.spawn({50, 0, 0}, 5, 1, 30.0f, true);
    for (int i = 0; i < 120; i++) magnetStep(b, 0, ship, 1.0f / 60, 120.0f, 140.0f);
    CHECK(close(b.px[0], 50.0)); CHECK(close(b.vx[0], 0.0));
    // it follows a moving ship: the capsule keeps closing while the ship flies away at 60 m/s
    CapsulePool m; m.init(2); m.spawn({60, 0, 0}, 5, 1, 30.0f);
    double gap0 = 60, gap = gap0; Vec3d s2{0, 0, 0};
    for (int i = 0; i < 60 * 4; i++) { s2.x -= 60.0 / 60; magnetStep(m, 0, s2, 1.0f / 60, 120.0f, 140.0f); gap = std::fabs(m.px[0] - s2.x); if (gap < 20) break; }
    CHECK(gap < 20.0);
    magnetStep(m, 0, s2, 0.0f, 120.0f, 140.0f);                                                       // dt 0: no NaN
    CHECK(m.px[0] == m.px[0]);
}

TEST(mining_capsule_is_collected_at_any_speed_and_partial_pickups_leave_a_remainder) {
    using namespace gameplay;
    CHECK(canScoopAny(15.0, 500.0, 20.0, 0.0));                        // scoop_speed 0: no speed limit at all
    CHECK(!canScoopAny(25.0, 1.0, 20.0, 0.0));                          // still needs contact
    CHECK(canScoopAny(20.0, 10.0, 20.0, 40.0)); CHECK(!canScoopAny(20.0, 41.0, 20.0, 40.0));       // a positive limit still works
    PickupOutcome all = afterPickup(41, 41);
    CHECK(all.removeCapsule); CHECK_EQ(all.remaining, 0); CHECK(!all.blocked);
    PickupOutcome part = afterPickup(41, 12);                             // the ore's hold only had 12 free
    CHECK(!part.removeCapsule); CHECK_EQ(part.remaining, 29); CHECK(part.blocked);                  // the rest stays in the capsule, red
    PickupOutcome none = afterPickup(41, 0);
    CHECK(!none.removeCapsule); CHECK_EQ(none.remaining, 41); CHECK(none.blocked);
    PickupOutcome odd = afterPickup(10, 99); CHECK(odd.removeCapsule); CHECK_EQ(odd.remaining, 0);   // a bogus "got" is clamped
    PickupOutcome neg = afterPickup(10, -5); CHECK_EQ(neg.remaining, 10);
}

TEST(mining_modes_and_where_the_ore_goes) {
    using namespace gameplay;
    Mode m;
    CHECK(parseMode("capsule", m) && m == Mode::Capsule); CHECK(parseMode("instant", m) && m == Mode::Instant); CHECK(parseMode("chunks", m) && m == Mode::Chunks);
    CHECK(!parseMode("banana", m)); CHECK(!parseMode("", m));
    // instant: always straight into the hold; capsule: straight in only when the ship is too far to ever collect it; chunks: never
    CHECK(goesStraightToHold(Mode::Instant, 10.0, 3000.0));
    CHECK(!goesStraightToHold(Mode::Capsule, 500.0, 3000.0)); CHECK(goesStraightToHold(Mode::Capsule, 3500.0, 3000.0));
    CHECK(!goesStraightToHold(Mode::Chunks, 9999.0, 3000.0));
    // instant mode with a nearly full hold: the part that did not fit becomes a (red) capsule, nothing is lost or invented
    int total = gameplay::totalYield(9.0f, 0.5f);                                                    // 41
    PickupOutcome o = afterPickup(total, 12);
    CHECK_EQ(o.remaining + 12, total); CHECK(o.blocked);
    CHECK_EQ(gameplay::totalYield(9.0f, 0.5f), 41);                                                  // capsule mode holds ALL of it in one capsule
}
