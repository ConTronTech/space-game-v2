#include <cmath>
#include "ship/gravity/gravity_rules.h"
#include "tests/test.h"

namespace {
using gravity::Vec3d;
bool near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol * std::max(1.0, std::fabs(b)); }
bool finiteV(const Vec3d& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// sun at the origin (r 1800), a planet at 40000 (r 400) with a moon 1500 out (r 50), a second planet at -80000 (r 300)
std::vector<world::Body> gravSystem() {
    std::vector<world::Body> b(4);
    b[0].id = 0; b[0].kind = world::BodyKind::Sun; b[0].radius = 1800; b[0].parent = -1;
    b[1].id = 1; b[1].kind = world::BodyKind::Planet; b[1].radius = 400; b[1].parent = 0; b[1].orbitRadius = 40000; b[1].position = {40000, 0, 0};
    b[2].id = 2; b[2].kind = world::BodyKind::Moon; b[2].radius = 50; b[2].parent = 1; b[2].orbitRadius = 1500; b[2].position = {41500, 0, 0};
    b[3].id = 3; b[3].kind = world::BodyKind::Planet; b[3].radius = 300; b[3].parent = 0; b[3].orbitRadius = 80000; b[3].position = {-80000, 0, 0};
    return b;
}
} // namespace

TEST(gravity_mu_is_the_orbit_lock_formula) {
    CHECK(near(gravity::bodyMu(400, 60), orbit::bodyMu(400, 60)));
    CHECK(near(gravity::bodyMu(400, 60), 60.0 * 400 * 400));
    // a circular orbit at the guide's speed is exactly balanced by this module's gravity: v^2 / r == |a|
    gravity::Source s; s.radius = 400; s.mu = gravity::bodyMu(400, 60);
    double r = 1000, v = orbit::circularSpeed(s.mu, r);
    CHECK(near(v * v / r, orbit::length(gravity::acceleration(s, {r, 0, 0}, 0, 0)), 1e-9));
}

TEST(gravity_soi_radius_hand_example) {
    // a = 40000, radius ratio 400/1800: (mu ratio)^0.4 = (400/1800)^0.8 = 0.30031...
    double soi = gravity::soiRadius(40000, gravity::bodyMu(400, 60), gravity::bodyMu(1800, 60));
    CHECK(near(soi, 40000 * std::pow(400.0 / 1800.0, 0.8), 1e-9));
    CHECK(near(soi, 12012.5, 1e-3));
    CHECK(near(gravity::soiRadius(0, 1, 1), 0));
    CHECK(near(gravity::soiRadius(100, 1, 0), 0));
    std::vector<gravity::Source> src;
    gravity::buildSources(gravSystem(), 60, 2, src);
    CHECK_EQ((int)src.size(), 4);
    CHECK(near(src[0].soi, 160000));                           // 2 x the outermost planet orbit (80000)
    CHECK(near(src[1].soi, soi, 1e-9));
    CHECK(near(src[2].soi, 1500 * std::pow(50.0 / 400.0, 0.8), 1e-9));   // moon relative to its planet
    CHECK(src[2].kind == 2 && src[1].kind == 1 && src[0].kind == 0);
}

TEST(gravity_dominant_body_nested_selection) {
    std::vector<gravity::Source> src;
    gravity::buildSources(gravSystem(), 60, 2, src);
    CHECK_EQ(gravity::dominantBody(src, {41600, 0, 0}, -1, 1.05), 2);    // inside the moon's SOI (~285): the moon, not the planet
    CHECK_EQ(gravity::dominantBody(src, {41000, 0, 3000}, -1, 1.05), 1); // in the planet's SOI, out of the moon's
    CHECK_EQ(gravity::dominantBody(src, {-20000, 0, 0}, -1, 1.05), 0);   // between the planets: the sun
    CHECK_EQ(gravity::dominantBody(src, {500000, 0, 0}, -1, 1.05), -1);  // far beyond sun_range: no gravity
    CHECK_EQ(gravity::dominantBody(src, {500000, 0, 0}, 0, 1.05), -1);
}

TEST(gravity_hysteresis_does_not_flicker) {
    std::vector<gravity::Source> src;
    gravity::buildSources(gravSystem(), 60, 2, src);
    double soi = src[1].soi;
    int cur = -1;
    cur = gravity::dominantBody(src, {40000 + soi * 0.99, 0, 0}, cur, 1.05);
    CHECK_EQ(cur, 1);
    int changes = 0;
    for (int i = 0; i < 40; i++) {                                         // jitter just around the boundary: 0.99 .. 1.04 x SOI
        double x = 40000 + soi * (i % 2 ? 1.04 : 0.99);
        int n = gravity::dominantBody(src, {x, 0, 0}, cur, 1.05);
        if (n != cur) changes++;
        cur = n;
    }
    CHECK_EQ(changes, 0);
    CHECK_EQ(gravity::dominantBody(src, {40000 + soi * 1.06, 0, 0}, cur, 1.05), 0);   // clearly out: the sun takes over
    CHECK_EQ(gravity::dominantBody(src, {40000 + soi * 1.02, 0, 0}, 0, 1.05), 0);     // re-entry needs the real SOI edge
    CHECK_EQ(gravity::dominantBody(src, {40000 + soi * 0.98, 0, 0}, 0, 1.05), 1);
}

TEST(gravity_acceleration_direction_and_clamps) {
    gravity::Source s; s.pos = {100, 0, 0}; s.radius = 400; s.mu = gravity::bodyMu(400, 60);
    Vec3d a = gravity::acceleration(s, {100, 1000, 0}, 0, 0);
    CHECK(near(a.y, -s.mu / 1e6, 1e-9) && near(a.x, 0) && near(a.z, 0));         // toward the body, mu / r^2
    // min radius = the body radius by default: at the surface or inside, never more than gravity_scale
    CHECK(near(orbit::length(gravity::acceleration(s, {100, 400, 0}, 0, 0)), 60, 1e-9));
    CHECK(near(orbit::length(gravity::acceleration(s, {100, 1, 0}, 0, 0)), 60, 1e-9));
    CHECK(near(orbit::length(gravity::acceleration(s, {100, 1, 0}, 10, 0)), s.mu / 100, 1e-9));   // explicit min_radius
    Vec3d c = gravity::acceleration(s, {100, 0, 0}, 0, 0);                         // exact centre: zero, not NaN
    CHECK(finiteV(c) && near(orbit::length(c), 0));
    Vec3d t = gravity::acceleration(s, {100, 1e-12, 0}, 1e-15, 0);
    CHECK(finiteV(t));
    CHECK(near(orbit::length(gravity::acceleration(s, {100, 1, 0}, 0.001, 200)), 200, 1e-9));   // max_accel clamp
}

TEST(gravity_symplectic_step_and_orbit) {
    Vec3d v = gravity::kick({1, 2, 3}, {0, -10, 0.5}, 0.1);
    CHECK(near(v.x, 1) && near(v.y, 1) && near(v.z, 3.05));
    // a circular orbit integrated for one period at 60 Hz keeps its radius (symplectic: no secular drift)
    gravity::Source s; s.radius = 400; s.mu = gravity::bodyMu(400, 60);
    double r = 1000, sp = orbit::circularSpeed(s.mu, r), dt = 1.0 / 60.0;
    Vec3d p{r, 0, 0}, vel{0, 0, sp};
    double lo = r, hi = r;
    int steps = (int)(2 * 3.14159265358979 * r / sp / dt);
    for (int i = 0; i < steps; i++) {
        vel = gravity::kick(vel, gravity::acceleration(s, p, 0, 200), dt);
        p = orbit::add(p, orbit::mul(vel, dt));
        double d = orbit::length(p); lo = std::min(lo, d); hi = std::max(hi, d);
    }
    CHECK(hi - lo < 2.0);
    auto oi = gravity::orbitInfo({r, 0, 0}, {0, 0, sp}, s.mu);
    CHECK(near(oi.ecc, 0, 1e-9) && near(oi.energy, -s.mu / (2 * r), 1e-9));
    Vec3d buf[61];
    int n = gravity::forecast(s, {r, 0, 0}, {0, 0, sp}, 0.5, 60, 0, 200, buf);
    CHECK_EQ(n, 61);
    CHECK(near(orbit::length(buf[60]), r, 0.01));
    n = gravity::forecast(s, {r, 0, 0}, {0, 0, 0}, 0.5, 60, 0, 200, buf);           // dropped: stops at the surface
    CHECK(n < 61 && n > 1);
}

TEST(gravity_skip_conditions) {
    CHECK(gravity::shouldApply(true, true, false, false, false, false));
    CHECK(!gravity::shouldApply(false, true, false, false, false, false));   // tunable off
    CHECK(!gravity::shouldApply(true, false, false, false, false, false));   // dead
    CHECK(!gravity::shouldApply(true, true, true, false, false, false));     // held (docking)
    CHECK(!gravity::shouldApply(true, true, false, true, false, false));     // warping
    CHECK(!gravity::shouldApply(true, true, false, false, true, false));     // orbit-locked
    CHECK(!gravity::shouldApply(true, true, false, false, false, true));     // paused
}

TEST(gravity_dock_assist_fade) {
    const double zone = 180, range = 720, lo = 0.1;
    CHECK(gravity::dockAssistFactor(720, zone, range, lo) == 1.0);          // at range: untouched
    CHECK(gravity::dockAssistFactor(5000, zone, range, lo) == 1.0);         // beyond
    CHECK(std::fabs(gravity::dockAssistFactor(180, zone, range, lo) - lo) < 1e-12);   // at the zone: the floor
    CHECK(std::fabs(gravity::dockAssistFactor(50, zone, range, lo) - lo) < 1e-12);
    CHECK(std::fabs(gravity::dockAssistFactor(450, zone, range, lo) - 0.55) < 1e-9);  // midpoint of smoothstep
    double prev = lo;                                                       // monotonic and continuous
    for (double d = 180; d <= 720; d += 1) {
        double f = gravity::dockAssistFactor(d, zone, range, lo);
        CHECK(f >= prev - 1e-12 && f - prev < 0.01);
        prev = f;
    }
    // degenerate inputs never NaN; bad range = no change
    CHECK(gravity::dockAssistFactor(-5, zone, range, lo) == lo);
    CHECK(gravity::dockAssistFactor(NAN, zone, range, lo) == 1.0);
    CHECK(gravity::dockAssistFactor(300, zone, 0, lo) == 1.0);
    CHECK(gravity::dockAssistFactor(300, zone, 100, lo) == 1.0);
    CHECK(gravity::dockAssistFactor(100, zone, range, -3) == 0.0);
    CHECK(gravity::dockAssistFactor(100, zone, range, 7) == 1.0);
    CHECK(gravity::dockAssistFactor(100, zone, range, 1.0) == 1.0);        // dock_assist_min 1 = the old behaviour
}

TEST(gravity_dock_assist_no_station_is_exactly_one) {
    CHECK(gravity::dockAssist(false, true, 10, 180, 720, 0.1) == 1.0);     // no station reported / IDocking absent
    CHECK(gravity::dockAssist(true, false, 10, 180, 720, 0.1) == 1.0);     // --gravity-dock-assist-off
    CHECK(std::fabs(gravity::dockAssist(true, true, 10, 180, 720, 0.1) - 0.1) < 1e-12);
}

TEST(gravity_gradient_distances_stay_inside_the_soi) {
    std::vector<gravity::Source> src;
    gravity::buildSources(gravSystem(), 60, 2, src);
    for (const auto& s : src) {
        double d[gravity::kGradientRings], m[gravity::kGradientRings];
        int n = gravity::gradientDistances(s, d, m);
        CHECK(n >= 1 && n <= gravity::kGradientRings);
        for (int i = 0; i < n; i++) {
            CHECK(d[i] > s.radius && d[i] < s.soi);
            CHECK(near(m[i] * s.radius, d[i]));
            if (i) CHECK(d[i] > d[i - 1]);
        }
    }
    gravity::Source big; big.radius = 400; big.soi = 1000;   // SOI inside 3x: the outer rings clamp onto one ring just inside it
    double d[gravity::kGradientRings];
    int n = gravity::gradientDistances(big, d);
    CHECK(n == 4 && d[3] < 1000 && d[3] > 990);
    gravity::Source none;                                    // no radius / no SOI: nothing to draw
    CHECK(gravity::gradientDistances(none, d) == 0);
}

TEST(gravity_gradient_magnitude_reuses_acceleration) {
    gravity::Source s; s.pos = {100, 5, -7}; s.radius = 400; s.mu = gravity::bodyMu(400, 60); s.soi = 1e5;
    CHECK(near(gravity::gradientMagnitude(s, 800, 0, 200), 60.0 / 4.0));                 // mu / r^2 = 60 (r/R)^-2
    CHECK(near(gravity::gradientMagnitude(s, 800, 0, 200), orbit::length(gravity::acceleration(s, {900, 5, -7}, 0, 200))));
    CHECK(near(gravity::gradientMagnitude(s, 440, 0, 10), 10.0));                         // the max_accel clamp applies too
}

TEST(gravity_gradient_colour_hot_strong_cool_weak_monotonic) {
    float c[3];
    gravity::gradientColor(gravity::gradientT(40, 1, 40), c);          // strongest -> hot red
    CHECK(std::fabs(c[0] - 1.0f) < 1e-5f && std::fabs(c[2] - 0.2f) < 1e-5f);
    gravity::gradientColor(gravity::gradientT(1, 1, 40), c);           // weakest -> cool blue
    CHECK(std::fabs(c[0] - 0.3f) < 1e-5f && std::fabs(c[2] - 1.0f) < 1e-5f);
    CHECK(std::fabs(gravity::gradientT(std::sqrt(40.0), 1, 40) - 0.5) < 1e-9);   // log scale
    float prev[3] = {0, 1, 2};
    for (double g = 1; g <= 40; g += 0.5) {
        gravity::gradientColor(gravity::gradientT(g, 1, 40), c);
        CHECK(c[0] >= prev[0] - 1e-6f && c[2] <= prev[2] + 1e-6f);
        for (int k = 0; k < 3; k++) prev[k] = c[k];
    }
    CHECK(gravity::gradientT(5, 3, 3) == 1.0 && gravity::gradientT(NAN, 1, 40) == 1.0);   // degenerate: hot, never NaN
    gravity::gradientColor(NAN, c);
    CHECK(std::isfinite(c[0]) && std::isfinite(c[1]) && std::isfinite(c[2]));
}
