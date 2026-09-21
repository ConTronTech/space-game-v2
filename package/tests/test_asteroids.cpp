#include <cmath>
#include <set>
#include "tests/test.h"
#include "world/asteroids/asteroid_rules.h"
#include "world/star_system/star_system_rules.h"

namespace {
using world::Vec3d;
bool close(double a, double b, double tol = 1e-4) { return std::fabs(a - b) <= tol * std::max(1.0, std::fabs(b)); }
double dist3(double ax, double ay, double az, const Vec3d& b) { return std::sqrt((ax - b.x) * (ax - b.x) + (ay - b.y) * (ay - b.y) + (az - b.z) * (az - b.z)); }

struct Sys {
    Vec3d sun;
    std::vector<double> orbits;
    std::vector<world::TargetBody> targets;
    Sys() {
        world::SystemParams p; p.seed = 1234;
        auto s = world::generateSystem(p);
        world::updatePositions(s, 0.4);          // real body positions, like IStarSystem gives
        sun = s.sun;
        for (auto& b : s.bodies) {
            if (b.kind == world::BodyKind::Planet) orbits.push_back(b.orbitRadius);
            if (b.kind != world::BodyKind::Sun) targets.push_back({b.position, b.radius, b.kind == world::BodyKind::Moon});
        }
    }
};
world::OreTable ores() { world::OreTable o; o.ids = {"iron", "gold"}; o.weights = {40, 8}; o.color = {0.4f, 0.35f, 0.3f, 0.8f, 0.7f, 0.2f}; return o; }
} // namespace

TEST(asteroids_generation_is_deterministic_and_counts_match) {
    Sys s; world::GenParams gp;
    auto a = world::generateField(gp, s.sun, s.orbits, s.targets, ores()), b = world::generateField(gp, s.sun, s.orbits, s.targets, ores());
    CHECK_EQ(a.count(), 1 * 1500 + 4 * 60);
    CHECK_EQ(a.count(), b.count());
    for (int i = 0; i < a.count(); i++) {
        CHECK(close(a.x[i], b.x[i], 1e-9)); CHECK(close(a.y[i], b.y[i], 1e-9)); CHECK(close(a.z[i], b.z[i], 1e-9));
        CHECK(close(a.radius[i], b.radius[i])); CHECK_EQ(a.ore[i], b.ore[i]); CHECK_EQ(a.variant[i], b.variant[i]);
    }
    gp.seed = 99;
    auto c = world::generateField(gp, s.sun, s.orbits, s.targets, ores());
    CHECK(std::fabs(c.x[0] - a.x[0]) > 1.0 || std::fabs(c.z[0] - a.z[0]) > 1.0);
    // every array has the same length, values are sane
    CHECK_EQ(a.y.size(), a.x.size()); CHECK_EQ(a.spinRate.size(), a.x.size()); CHECK_EQ(a.ore.size(), a.x.size()); CHECK_EQ(a.variant.size(), a.x.size());
    for (int i = 0; i < a.count(); i++) {
        CHECK(a.radius[i] >= 0.5f && a.radius[i] <= 120.0f);
        CHECK(a.variant[i] < world::kAsteroidVariants);
        CHECK(a.ore[i] < 2);
        CHECK(close(std::sqrt(a.ax[i] * a.ax[i] + a.ay[i] * a.ay[i] + a.az[i] * a.az[i]), 1.0, 1e-4));
        CHECK(a.x[i] == a.x[i] && a.y[i] == a.y[i] && a.z[i] == a.z[i]);
    }
}

TEST(asteroids_belt_lies_between_two_orbits_in_the_orbital_plane) {
    Sys s; world::GenParams gp; gp.clusterCount = 0;
    std::vector<world::BeltInfo> belts;
    auto f = world::generateField(gp, s.sun, s.orbits, s.targets, ores(), &belts);
    CHECK_EQ(belts.size(), (size_t)1);
    CHECK_EQ(f.count(), 1500);
    std::vector<double> sorted = s.orbits; std::sort(sorted.begin(), sorted.end());
    bool between = false;
    for (size_t i = 0; i + 1 < sorted.size(); i++) if (belts[0].inner > sorted[i] && belts[0].outer < sorted[i + 1]) between = true;
    CHECK(between);
    CHECK(belts[0].outer - belts[0].inner <= gp.beltMaxWidth + 1e-6);
    for (int i = 0; i < f.count(); i++) {
        double dx = f.x[i] - s.sun.x, dz = f.z[i] - s.sun.z, r = std::sqrt(dx * dx + dz * dz);
        CHECK(r >= belts[0].inner - 1e-6 && r <= belts[0].outer + 1e-6);
        CHECK(std::fabs(f.y[i] - s.sun.y) <= belts[0].halfHeight + 1e-6);
    }
}

TEST(asteroids_clusters_surround_their_bodies_and_tiers_are_sensible) {
    Sys s; world::GenParams gp; gp.beltCount = 0; gp.clusterCount = 4; gp.clusterAsteroids = 60;
    auto f = world::generateField(gp, s.sun, s.orbits, s.targets, ores());
    CHECK_EQ(f.count(), 240);
    // every asteroid sits in the (flattened) shell of at least one target body
    int small = 0;
    for (int i = 0; i < f.count(); i++) {
        bool inShell = false;
        for (auto& t : s.targets) {
            double d = dist3(f.x[i], f.y[i], f.z[i], t.pos);
            double inner = t.moon ? t.radius * 3.0 + 100.0 : t.radius * 4.5 + 500.0;
            if (d >= inner * 0.45 - 1.0 && d <= inner + std::max(400.0, t.radius * 3.0) + 1.0) inShell = true;   // y is flattened, so allow for that
        }
        CHECK(inShell);
        if (f.radius[i] < 5.0f) small++;
    }
    CHECK(small > 200);                                             // clusters are mostly small rocks
    // belt size tiers: about half small, some medium, few big
    world::GenParams bp; bp.clusterCount = 0; bp.beltAsteroids = 4000;
    auto b = world::generateField(bp, s.sun, s.orbits, s.targets, ores());
    int sm = 0, med = 0, big = 0;
    for (float r : b.radius) { if (r < 5.0f) sm++; else if (r < 30.0f) med++; else big++; }
    CHECK(sm > 1700 && sm < 2300);
    CHECK(med > 1000);
    CHECK(big > 100 && big < 800);
}

TEST(asteroids_no_star_system_or_zero_counts_generate_nothing) {
    world::GenParams gp;
    CHECK_EQ(world::generateField(gp, {}, {}, {}, ores()).count(), 0);            // no orbits, no targets
    CHECK_EQ(world::generateField(gp, {}, {50000.0}, {}, ores()).count(), 0);     // one orbit: no gap for a belt
    Sys s; gp.beltAsteroids = 0; gp.clusterAsteroids = 0;
    CHECK_EQ(world::generateField(gp, s.sun, s.orbits, s.targets, ores()).count(), 0);
    gp.beltAsteroids = 100; gp.beltCount = 50; gp.clusterCount = 0;                // more belts than gaps: as many as fit
    CHECK(world::generateField(gp, s.sun, s.orbits, s.targets, ores()).count() <= 100 * (int)s.orbits.size());
}

TEST(asteroids_ore_pick_follows_the_weights) {
    std::vector<float> w = {40, 8, 2};
    int c[3] = {0, 0, 0};
    for (int i = 0; i < 5000; i++) c[world::pickOre(w, (i + 0.5f) / 5000.0f)]++;
    CHECK(std::abs(c[0] - 4000) < 20); CHECK(std::abs(c[1] - 800) < 20); CHECK(std::abs(c[2] - 200) < 20);
    CHECK_EQ(world::pickOre({}, 0.5f), 0);
    CHECK_EQ(world::pickOre({0, 0}, 0.5f), 0);
    CHECK_EQ(world::pickOre(w, 1.0f), 2);                            // r = 1 is clamped: last entry, no overflow
}

TEST(asteroids_meshes_have_the_right_counts_and_are_valid) {
    for (int l = 0; l < world::kAsteroidLevels; l++) {
        auto m = world::buildAsteroidMesh(l, 7);
        CHECK_EQ(m.triangleCount(), 20 * (1 << (2 * l)));            // 20, 80, 320
        CHECK_EQ(m.vertexCount(), 10 * (1 << (2 * l)) + 2);
        for (auto i : m.indices) CHECK((int)i < m.vertexCount());
        for (int i = 0; i < m.vertexCount(); i++) {
            const float* v = &m.verts[i * 6];
            double r = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            CHECK(r > 0.3 && r < 1.8);                                // lumpy but bounded (so radius*0.9 physics is a fair sphere)
            CHECK(close(std::sqrt(v[3] * v[3] + v[4] * v[4] + v[5] * v[5]), 1.0, 1e-3));
            for (int k = 0; k < 6; k++) CHECK(v[k] == v[k]);
        }
    }
    auto a = world::buildAsteroidMesh(2, 7), b = world::buildAsteroidMesh(2, 7), c = world::buildAsteroidMesh(2, 8);
    double same = 0, other = 0;
    for (size_t i = 0; i < a.verts.size(); i++) { same = std::max(same, (double)std::fabs(a.verts[i] - b.verts[i])); other = std::max(other, (double)std::fabs(a.verts[i] - c.verts[i])); }
    CHECK(same < 1e-6); CHECK(other > 1e-3);
    CHECK_EQ(world::asteroidTriangles(9), 320);                       // clamped
}

TEST(asteroids_view_cone_culling) {
    double cosHalf = std::cos(0.9);                                    // about 51 degrees
    CHECK(world::inViewCone(1000, 1000, 10, cosHalf));                 // dead ahead
    CHECK(!world::inViewCone(1000, -1000, 10, cosHalf));               // behind
    CHECK(world::inViewCone(1000, 1000 * std::cos(0.85), 10, cosHalf)); // inside the cone
    CHECK(!world::inViewCone(1000, 1000 * std::cos(1.2), 10, cosHalf)); // well outside
    CHECK(world::inViewCone(1000, 1000 * std::cos(0.92), 50, cosHalf)); // just outside the axis cone but the sphere overlaps it
    CHECK(world::inViewCone(5, -5, 10, cosHalf));                      // inside the sphere itself: always visible
    double half = world::viewConeHalfAngle(90.0, 16.0 / 9.0);
    CHECK(half > 0.785 && half < 1.3);                                 // wider than the vertical half fov, less than 90 degrees
}

TEST(asteroids_lod_and_budget) {
    CHECK_EQ(world::asteroidLod(1.0f, 10.0f, 2.0f), -1);               // a speck: point
    CHECK_EQ(world::asteroidLod(3.0f, 10.0f, 2.0f), 0);
    CHECK(world::asteroidLod(500.0f, 10.0f, 2.0f) == 2);               // capped at level 2
    CHECK(world::asteroidLod(20.0f, 10.0f, 2.0f) >= world::asteroidLod(5.0f, 10.0f, 2.0f));
    std::vector<int> lv(100, 2);                                       // 100 x 320 = 32000 triangles
    int t = world::applyAsteroidBudget(lv, 15000);
    CHECK(t <= 15000);
    int sum = 0; for (int l : lv) sum += l < 0 ? 0 : world::asteroidTriangles(l);
    CHECK_EQ(sum, t);
    CHECK(lv.front() >= lv.back());                                    // the nearest keep at least as much detail as the farthest
    std::vector<int> ok(3, 1);
    CHECK_EQ(world::applyAsteroidBudget(ok, 15000), 3 * 80);           // fits: untouched
    std::vector<int> tight(10, 0);
    int tt = world::applyAsteroidBudget(tight, 100);                   // even level 0 (200 triangles) does not fit: the far ones become points
    CHECK(tt <= 100); CHECK_EQ(tight.front(), 0); CHECK_EQ(tight.back(), -1);
}

TEST(asteroids_nearest_and_physics_pool_hysteresis) {
    Sys s; world::GenParams gp;
    auto f = world::generateField(gp, s.sun, s.orbits, s.targets, ores());
    Vec3d p{f.x[10] + 1.0, f.y[10], f.z[10]};
    std::vector<int> out;
    world::nearestIndices(f, p, 5, out);
    CHECK_EQ((int)out.size(), 5); CHECK_EQ(out[0], 10);
    double last = -1;
    for (int i : out) { double d = dist3(f.x[i], f.y[i], f.z[i], p); CHECK(d >= last - 1e-9); last = d; }
    world::nearestIndices(f, p, 1000000, out); CHECK_EQ((int)out.size(), f.count());   // n larger than the field
    world::nearestIndices(f, p, 0, out); CHECK(out.empty());
    // pool: register inside R, keep until 1.2 R, then remove; no flapping around R
    CHECK_EQ(world::poolAction(1400, false, 1500, 1.2), 1);
    CHECK_EQ(world::poolAction(1600, false, 1500, 1.2), 0);            // outside: not registered
    CHECK_EQ(world::poolAction(1600, true, 1500, 1.2), 0);             // registered, between R and 1.2 R: stays
    CHECK_EQ(world::poolAction(1801, true, 1500, 1.2), -1);            // beyond 1.2 R: removed
    CHECK_EQ(world::poolAction(1500, true, 1500, 0.5), 0);             // hysteresis below 1 counts as 1
}
