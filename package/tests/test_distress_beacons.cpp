#include <cmath>
#include "world/distress_beacons/beacon_rules.h"
#include "tests/test.h"

namespace {
bool closeB(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol * std::max(1.0, std::fabs(b)); }
world::BeaconParams smallBeacon() {
    world::BeaconParams p;
    p.intervalMin = 10; p.intervalMax = 20; p.lifetime = 30; p.detectRange = 50000; p.investigateRange = 75;
    return p;
}
world::BeaconWorld worldFor(const world::System& sys, bool belt) {
    world::BeaconWorld w;
    w.sun = sys.sun;
    for (const auto& b : sys.bodies) w.bodies.push_back({b.id, b.kind, b.radius, b.orbitRadius, b.parent});
    w.stations.push_back({1, sys.bodies.size() > 1 ? sys.bodies[1].radius * 2.0 : 1000.0});
    if (belt) for (int i = 0; i < 20; i++) w.belt.push_back({sys.sun.x + 60000.0 + i * 500.0, sys.sun.y, sys.sun.z});
    return w;
}
} // namespace

TEST(distress_beacons_interval_is_seeded_and_in_range) {
    for (uint32_t seed : {1u, 1234u, 0xFFFFFFFFu})
        for (uint32_t cycle = 0; cycle < 50; cycle++) {
            double a = world::beaconInterval(seed, cycle, 60, 180), b = world::beaconInterval(seed, cycle, 60, 180);
            CHECK_EQ(a, b);
            CHECK(a >= 60 && a < 180);
        }
    CHECK(world::beaconInterval(1234, 0, 60, 180) != world::beaconInterval(1234, 1, 60, 180));
    CHECK(world::beaconInterval(1234, 0, 60, 180) != world::beaconInterval(4321, 0, 60, 180));
    CHECK_EQ(world::beaconInterval(7, 3, 90, 90), 90.0);
    double s = world::beaconInterval(7, 3, 180, 60);                 // swapped bounds still land in range
    CHECK(s >= 60 && s < 180);
    CHECK(world::beaconSiteSeed(1234, 0) != world::beaconSiteSeed(1234, 1));
}

TEST(distress_beacons_sanitize_params) {
    world::BeaconParams p;
    p.intervalMin = -5; p.intervalMax = 0; p.lifetime = NAN; p.detectRange = -1; p.investigateRange = 0;
    world::BeaconParams s = world::sanitizeBeaconParams(p);
    CHECK(s.intervalMin >= 1 && s.intervalMax >= s.intervalMin);
    CHECK_EQ(s.lifetime, world::BeaconParams{}.lifetime);
    CHECK_EQ(s.detectRange, 0.0);
    CHECK(s.investigateRange >= 1);
}

TEST(distress_beacons_ranges) {
    CHECK(world::beaconDetected(1000, 5000, true));
    CHECK(world::beaconDetected(5000, 5000, true));
    CHECK(!world::beaconDetected(5001, 5000, true));
    CHECK(!world::beaconDetected(10, 5000, false));                 // nothing to detect when none is active
    CHECK(!world::beaconDetected(NAN, 5000, true));
    CHECK(world::beaconInvestigates(75, 75, true));
    CHECK(!world::beaconInvestigates(76, 75, true));
    CHECK(!world::beaconInvestigates(0, 75, false));
    CHECK(!world::beaconInvestigates(NAN, 75, true));
}

TEST(distress_beacons_candidates_are_deterministic_and_use_every_zone) {
    for (unsigned seed : {1234u, 7u, 99999u}) {
        world::SystemParams sp; sp.seed = seed;
        world::System sys = world::generateSystem(sp); world::updatePositions(sys, 0.0);
        const world::BeaconWorld w = worldFor(sys, true);
        auto a = world::beaconCandidates(w, 555), b = world::beaconCandidates(w, 555), c = world::beaconCandidates(w, 556);
        CHECK_EQ(a.size(), (size_t)world::kBeaconCandidates);
        bool differs = false, zone[3] = {false, false, false};
        for (size_t i = 0; i < a.size(); i++) {
            CHECK(a[i].zone == b[i].zone && a[i].anchor == b[i].anchor);
            CHECK_EQ(a[i].offset.x, b[i].offset.x);
            if (a[i].offset.x != c[i].offset.x) differs = true;
            zone[(int)a[i].zone] = true;
            CHECK(std::isfinite(a[i].offset.x) && std::isfinite(a[i].offset.y) && std::isfinite(a[i].offset.z));
            if (a[i].zone == world::BeaconZone::NearBody) {             // clear of the body's surface
                CHECK(a[i].anchor > 0);
                const double r = sys.bodies[(size_t)a[i].anchor].radius;
                CHECK(world::bdetail::dist(a[i].offset, {}) > r + world::kBeaconBodyMargin * 0.99);
            }
            if (a[i].zone == world::BeaconZone::DeepSpace) {            // beyond the sun, inside the planet rings
                CHECK(world::bdetail::dist(a[i].offset, sys.sun) > sys.bodies[0].radius * 5.0);
            }
        }
        CHECK(differs);                                               // a new site seed moves the beacon
        CHECK(zone[0] && zone[2]);                                    // deep space and belt always place; near-body may fall back
        auto nb = world::beaconCandidates(worldFor(sys, false), 555);
        for (const auto& s : nb) CHECK(s.zone != world::BeaconZone::Belt);   // no belt: no belt sites
    }
    world::BeaconWorld empty;                                         // no bodies at all still gives usable deep-space sites
    auto e = world::beaconCandidates(empty, 1);
    CHECK_EQ(e.size(), (size_t)world::kBeaconCandidates);
    for (const auto& s : e) CHECK(s.zone == world::BeaconZone::DeepSpace && std::isfinite(s.offset.x));
}

TEST(distress_beacons_position_follows_anchor) {
    world::BeaconSite fixed; fixed.offset = {10, 20, 30};
    world::Vec3d p = world::beaconPosition(fixed, {1000, 0, 0});
    CHECK_EQ(p.x, 10.0);
    world::BeaconSite near; near.anchor = 2; near.offset = {10, 20, 30};
    p = world::beaconPosition(near, {1000, 0, 0});
    CHECK_EQ(p.x, 1010.0); CHECK_EQ(p.y, 20.0);
}

TEST(distress_beacons_pick_prefers_reachable) {
    const world::Vec3d ship{0, 0, 0};
    std::vector<world::Vec3d> pos = {{900000, 0, 0}, {20000, 0, 0}, {30, 0, 0}, {40000, 0, 0}};
    for (uint32_t seed = 0; seed < 40; seed++) {
        int i = world::pickBeaconCandidate(pos, ship, 50000, 75, seed);
        CHECK(i == 1 || i == 3);                                      // inside detect range, never on top of the ship
    }
    std::vector<world::Vec3d> far = {{900000, 0, 0}, {300000, 0, 0}, {10, 0, 0}};
    CHECK_EQ(world::pickBeaconCandidate(far, ship, 50000, 75, 1), 1);   // none in range: the nearest one not on top of the ship
    std::vector<world::Vec3d> onTop = {{10, 0, 0}, {20, 0, 0}};
    CHECK_EQ(world::pickBeaconCandidate(onTop, ship, 50000, 75, 1), 0);
    CHECK_EQ(world::pickBeaconCandidate({}, ship, 50000, 75, 1), -1);
}

TEST(distress_beacons_cycle_spawn_then_expire) {
    const world::BeaconParams p = smallBeacon();
    world::BeaconState s = world::startBeaconCycle(42, p);
    const double gap = s.untilSpawn;
    CHECK(gap >= 10 && gap < 20);
    CHECK_EQ(world::beaconEta(s), -1.0);
    const double dt = 1.0 / 60.0;
    int spawned = 0, expired = 0;
    double t = 0, spawnAt = -1, expireAt = -1, lastEta = 1e9;
    for (int i = 0; i < 60 * 120 && expired == 0; i++) {
        world::BeaconStep st = world::stepBeacon(s, p, dt);
        t += dt;
        if (st.spawned) { spawned++; spawnAt = t; CHECK(s.active); }
        if (s.active) { CHECK(world::beaconEta(s) <= lastEta); lastEta = world::beaconEta(s); }
        if (st.expired) { expired++; expireAt = t; }
    }
    CHECK_EQ(spawned, 1);
    CHECK_EQ(expired, 1);
    CHECK(closeB(spawnAt, gap, 0.01));
    CHECK(closeB(expireAt - spawnAt, p.lifetime, 0.01));
    CHECK(!s.active);
    CHECK_EQ(s.cycle, 1u);
    CHECK_EQ(world::beaconEta(s), -1.0);
    CHECK(closeB(s.untilSpawn, world::beaconInterval(42, 1, p.intervalMin, p.intervalMax)));
}

TEST(distress_beacons_claim_ends_it_once_and_rolls_the_next) {
    const world::BeaconParams p = smallBeacon();
    world::BeaconState s = world::startBeaconCycle(9, p);
    CHECK(!world::claimBeacon(s, p));                                 // nothing to claim while waiting
    CHECK_EQ(s.cycle, 0u);
    world::BeaconStep st = world::stepBeacon(s, p, s.untilSpawn + 5.0);   // overshoot counts as beacon time
    CHECK(st.spawned && !st.expired);
    CHECK(closeB(world::beaconEta(s), p.lifetime - 5.0));
    CHECK(world::claimBeacon(s, p));
    CHECK(!s.active);
    CHECK_EQ(s.cycle, 1u);
    CHECK(!world::claimBeacon(s, p));                                 // one-time: a second claim does nothing
    CHECK_EQ(s.cycle, 1u);
    world::BeaconStep z = world::stepBeacon(s, p, 0);                  // dt 0 (or NaN) changes nothing
    CHECK(!z.spawned && !z.expired);
    z = world::stepBeacon(s, p, NAN);
    CHECK(!z.spawned && !z.expired);
    CHECK(closeB(s.untilSpawn, world::beaconInterval(9, 1, p.intervalMin, p.intervalMax)));
}

TEST(distress_beacons_huge_step_still_shows_the_beacon_once) {
    const world::BeaconParams p = smallBeacon();
    world::BeaconState s = world::startBeaconCycle(3, p);
    world::BeaconStep st = world::stepBeacon(s, p, s.untilSpawn + p.lifetime * 10);
    CHECK(st.spawned && !st.expired);                                 // live for one step, never spawned and expired unseen
    CHECK(s.active && world::beaconEta(s) > 0);
    st = world::stepBeacon(s, p, 1.0 / 60.0);
    CHECK(st.expired && !s.active);
}
