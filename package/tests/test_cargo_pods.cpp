#include <cmath>
#include "world/cargo_pods/pod_rules.h"
#include "tests/test.h"

namespace {
world::PodParams smallPods() {
    world::PodParams p;
    p.poolSize = 5; p.respawnMin = 10; p.respawnMax = 20; p.lifetime = 30;
    return p;
}
world::PodWorld podWorldFor(const world::System& sys, bool belt) {
    world::PodWorld w;
    w.sun = sys.sun;
    for (const auto& b : sys.bodies) w.bodies.push_back({b.id, b.kind, b.radius, b.orbitRadius, b.parent});
    w.stations.push_back({1, sys.bodies.size() > 1 ? sys.bodies[1].radius * 2.0 : 1000.0});
    if (belt) for (int i = 0; i < 20; i++) w.belt.push_back({sys.sun.x + 60000.0 + i * 500.0, sys.sun.y, sys.sun.z});
    return w;
}
} // namespace

TEST(cargo_pods_interval_is_seeded_and_in_range) {
    for (uint32_t seed : {1u, 1234u, 0xFFFFFFFFu})
        for (int slot = 0; slot < 8; slot++) {
            const uint32_t ss = world::podSlotSeed(seed, slot);
            const double first = world::podInterval(ss, 0, 60, 240);
            CHECK(first >= 0 && first < 60);                          // boot: the field fills within respawn_min
            for (uint32_t cycle = 1; cycle < 30; cycle++) {
                double a = world::podInterval(ss, cycle, 60, 240), b = world::podInterval(ss, cycle, 60, 240);
                CHECK_EQ(a, b);
                CHECK(a >= 60 && a < 240);
            }
        }
    CHECK(world::podSlotSeed(1234, 0) != world::podSlotSeed(1234, 1));   // slots are independent
    CHECK(world::podSiteSeed(77, 0) != world::podSiteSeed(77, 1));
    double s = world::podInterval(5, 3, 240, 60);                   // swapped bounds still land in range
    CHECK(s >= 60 && s < 240);
}

TEST(cargo_pods_sanitize_params) {
    world::PodParams p;
    p.poolSize = 500; p.respawnMin = -5; p.respawnMax = 0; p.lifetime = NAN; p.oreMin = 9; p.oreMax = 3; p.itemChance = 7;
    p.drift = -1; p.magnetRadius = 0; p.scoopRadius = NAN; p.scoopSpeed = -3;
    world::PodParams s = world::sanitizePodParams(p);
    CHECK_EQ(s.poolSize, world::kPodPoolMax);
    CHECK(s.respawnMin >= 1 && s.respawnMax >= s.respawnMin);
    CHECK_EQ(s.lifetime, world::PodParams{}.lifetime);
    CHECK(s.oreMax >= s.oreMin);
    CHECK_EQ(s.itemChance, 1.0);
    CHECK_EQ(s.drift, 0.0);
    CHECK(s.magnetRadius >= 1);
    CHECK_EQ(s.scoopRadius, world::PodParams{}.scoopRadius);
    CHECK_EQ(s.scoopSpeed, 0.0);
    p = world::PodParams{}; p.poolSize = -2; p.lifetime = 0;
    s = world::sanitizePodParams(p);
    CHECK_EQ(s.poolSize, 0);
    CHECK_EQ(s.lifetime, 0.0);                                      // 0 = persist until collected
}

TEST(cargo_pods_slots_run_independently) {
    const world::PodParams p = smallPods();
    std::vector<world::PodSlot> slots = world::startPodPool(1234, p);
    CHECK_EQ(slots.size(), (size_t)5);
    CHECK_EQ(world::activePods(slots), 0);
    int spawns = 0, expiries = 0;
    double firstSpawnAt[5] = {-1, -1, -1, -1, -1};
    double t = 0;
    for (int step = 0; step < 6000; step++) {                         // 100 s at 60 Hz
        t += 1.0 / 60.0;
        for (size_t i = 0; i < slots.size(); i++) {
            world::PodStep st = world::stepPodSlot(slots[i], p, 1.0 / 60.0);
            if (st.spawned) { spawns++; if (firstSpawnAt[i] < 0) firstSpawnAt[i] = t; }
            if (st.expired) { expiries++; CHECK(!slots[i].active); CHECK(slots[i].untilSpawn >= p.respawnMin); }
        }
    }
    for (double f : firstSpawnAt) CHECK(f > 0 && f <= p.respawnMin + 0.05);   // every slot filled within respawn_min
    bool staggered = false;
    for (int i = 1; i < 5; i++) if (std::fabs(firstSpawnAt[i] - firstSpawnAt[0]) > 0.1) staggered = true;
    CHECK(staggered);                                                 // not synced: pods appear at different times
    CHECK(spawns > 5);                                                // slots respawned after expiring
    CHECK(expiries > 0);
    std::vector<world::PodSlot> again = world::startPodPool(1234, p);  // same seed -> same timers
    for (size_t i = 0; i < slots.size(); i++) CHECK_EQ(again[i].untilSpawn, world::startPodPool(1234, p)[i].untilSpawn);
}

TEST(cargo_pods_collect_and_persist) {
    world::PodParams p = smallPods();
    world::PodSlot s = world::startPodPool(9, p)[0];
    CHECK(!world::collectPod(s, p));                                  // nothing to collect while waiting
    world::PodStep st = world::stepPodSlot(s, p, 1000.0);               // one huge step: spawns, still live
    CHECK(st.spawned && s.active && s.lifeLeft > 0);
    CHECK_EQ(s.cycle, 0u);
    CHECK(world::collectPod(s, p));
    CHECK(!s.active);
    CHECK_EQ(s.cycle, 1u);
    CHECK(s.untilSpawn >= p.respawnMin && s.untilSpawn < p.respawnMax);
    CHECK(!world::stepPodSlot(s, p, 0).spawned);                      // dt 0 changes nothing
    CHECK(!world::stepPodSlot(s, p, NAN).spawned);

    p.lifetime = 0;                                                   // persist: never expires
    world::PodSlot q = world::startPodPool(9, p)[0];
    world::stepPodSlot(q, p, 1000.0);
    CHECK(q.active);
    for (int i = 0; i < 100; i++) CHECK(!world::stepPodSlot(q, p, 100.0).expired);
    CHECK(q.active);
}

TEST(cargo_pods_candidates_are_deterministic_and_zoned) {
    for (unsigned seed : {1234u, 7u, 99999u}) {
        world::SystemParams sp; sp.seed = seed;
        world::System sys = world::generateSystem(sp); world::updatePositions(sys, 0.0);
        const world::PodWorld w = podWorldFor(sys, true);
        auto a = world::podCandidates(w, 555), b = world::podCandidates(w, 555), c = world::podCandidates(w, 556);
        CHECK_EQ(a.size(), (size_t)3);
        CHECK(a[0].zone == world::PodZone::DeepSpace && a[2].zone == world::PodZone::Belt);
        bool differs = false;
        for (size_t i = 0; i < a.size(); i++) {
            CHECK(a[i].zone == b[i].zone && a[i].anchor == b[i].anchor);
            CHECK_EQ(a[i].offset.x, b[i].offset.x);
            if (a[i].offset.x != c[i].offset.x) differs = true;
            CHECK(std::isfinite(a[i].offset.x) && std::isfinite(a[i].offset.y) && std::isfinite(a[i].offset.z));
            if (a[i].zone == world::PodZone::NearBody) {
                CHECK(a[i].anchor > 0);
                CHECK(world::pdetail::len(a[i].offset) > sys.bodies[(size_t)a[i].anchor].radius + world::kPodBodyMargin * 0.99);
            }
            if (a[i].zone == world::PodZone::DeepSpace) CHECK(world::pdetail::dist(a[i].offset, sys.sun) > sys.bodies[0].radius * 5.0);
        }
        CHECK(differs);
        auto nb = world::podCandidates(podWorldFor(sys, false), 555);
        CHECK_EQ(nb.size(), (size_t)2);
        for (const auto& s : nb) CHECK(s.zone != world::PodZone::Belt);
    }
    auto e = world::podCandidates(world::PodWorld{}, 1);              // no bodies at all: still usable deep-space sites
    CHECK_EQ(e.size(), (size_t)2);
    for (const auto& s : e) CHECK(s.zone == world::PodZone::DeepSpace && std::isfinite(s.offset.x));
}

TEST(cargo_pods_pick_avoids_the_ship) {
    std::vector<world::Vec3d> pos = {{0, 0, 0}, {50, 0, 0}, {5000, 0, 0}};
    for (uint32_t seed = 0; seed < 50; seed++) CHECK_EQ(world::pickPodCandidate(pos, {0, 0, 0}, 240, seed), 2);   // only one is clear
    std::vector<world::Vec3d> close = {{10, 0, 0}, {100, 0, 0}, {30, 0, 0}};
    CHECK_EQ(world::pickPodCandidate(close, {0, 0, 0}, 240, 3), 1);    // none clear: the farthest
    CHECK_EQ(world::pickPodCandidate({}, {0, 0, 0}, 240, 3), -1);
    std::vector<world::Vec3d> far = {{1e4, 0, 0}, {2e4, 0, 0}, {3e4, 0, 0}};
    bool seen[3] = {false, false, false};
    for (uint32_t seed = 0; seed < 200; seed++) seen[world::pickPodCandidate(far, {0, 0, 0}, 240, seed)] = true;
    CHECK(seen[0] && seen[1] && seen[2]);                             // all clear: the zone is a seeded choice
    CHECK_EQ(world::podPosition(world::PodSite{world::PodZone::NearBody, 2, {10, 0, 0}}, {1000, 5, 0}).x, 1010.0);
    CHECK_EQ(world::podPosition(world::PodSite{world::PodZone::DeepSpace, -1, {10, 0, 0}}, {1000, 5, 0}).x, 10.0);
}

TEST(cargo_pods_drift_is_slow_and_seeded) {
    for (uint32_t s = 0; s < 100; s++) {
        world::Vec3d d = world::podDrift(s, 2.0), e = world::podDrift(s, 2.0);
        CHECK_EQ(d.x, e.x);
        const double l = world::pdetail::len(d);
        CHECK(l >= 0.25 * 2.0 - 1e-6 && l <= 2.0 + 1e-6);
    }
    CHECK_EQ(world::pdetail::len(world::podDrift(5, 0)), 0.0);
}

TEST(cargo_pods_reward_is_weighted_modest_and_seeded) {
    const std::vector<float> weights = {40, 25, 15, 8, 12, 5, 3, 2};   // data/ores.json rarities
    int hits[8] = {}, items = 0;
    for (uint32_t s = 0; s < 4000; s++) {
        world::PodReward r = world::rollPodReward(s, weights, 2, 6, 0.15, 2), again = world::rollPodReward(s, weights, 2, 6, 0.15, 2);
        CHECK(r.ore == again.ore && r.amount == again.amount && r.item == again.item);
        CHECK(r.ore >= 0 && r.ore < 8);
        CHECK(r.amount >= 2 && r.amount <= 6);
        CHECK(r.item >= -1 && r.item < 2);
        hits[r.ore]++;
        if (r.item >= 0) items++;
    }
    CHECK(hits[0] > hits[7] * 5);                                     // common ore far more often than the rarest
    CHECK(items > 400 && items < 800);                                // ~15%
    CHECK_EQ(world::rollPodReward(1, weights, 2, 6, 1.0, 0).item, -1);   // no item list: never an item
    CHECK_EQ(world::rollPodReward(1, weights, 2, 6, 0.0, 3).item, -1);
    world::PodReward none = world::rollPodReward(1, {}, 2, 6, 0.0, 0);
    CHECK(none.ore == -1 && none.amount == 0);
    world::PodReward zero = world::rollPodReward(1, weights, 0, 0, 0.0, 0);
    CHECK(zero.ore == -1 && zero.amount == 0);
}

TEST(cargo_pods_magnet_and_scoop) {
    const world::Vec3d drift{1, 0, 0};
    // outside the magnet: eases to the drift
    world::Vec3d v = world::podMagnetVelocity({0, 0, 0}, drift, {0, 0, 0}, {500, 0, 0}, 1.0, 120, 140, false);
    CHECK(v.x > 0.9 && v.x <= 1.0);
    // inside: pulled toward the ship, and a pod stepped with it closes in and gets scooped
    world::Vec3d pos{0, 0, 0}, vel = drift;
    const world::Vec3d ship{100, 0, 0};
    bool scooped = false;
    for (int i = 0; i < 600 && !scooped; i++) {
        vel = world::podMagnetVelocity(vel, drift, pos, ship, 1.0 / 60.0, 120, 140, false);
        pos = world::pdetail::add(pos, world::pdetail::mul(vel, 1.0 / 60.0));
        scooped = world::podCanScoop(world::pdetail::dist(pos, ship), world::pdetail::len(vel), 20, 0);
    }
    CHECK(scooped);
    CHECK(world::pdetail::len(vel) <= 140 + 1e-6);
    // blocked (hold full): stops dead
    world::Vec3d b = world::podMagnetVelocity({5, 5, 5}, drift, {0, 0, 0}, {50, 0, 0}, 1.0 / 60.0, 120, 140, true);
    CHECK_EQ(world::pdetail::len(b), 0.0);
    // the scoop: radius, and the optional speed limit
    CHECK(world::podCanScoop(20, 999, 20, 0));
    CHECK(!world::podCanScoop(20.5, 0, 20, 0));
    CHECK(!world::podCanScoop(5, 50, 20, 40));
    CHECK(world::podCanScoop(5, 30, 20, 40));
    CHECK(!world::podCanScoop(NAN, 0, 20, 0));
}
