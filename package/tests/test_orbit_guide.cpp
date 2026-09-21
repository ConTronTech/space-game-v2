#include <cmath>
#include "ship/orbit_lock/orbit_lock_rules.h"
#include "tests/test.h"

using namespace orbit;

namespace {
bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }
constexpr double kPi = 3.14159265358979323846;
// a 400-unit planet at the origin, ship 1000 units out on +x; mu from the default gravity scale
Orbit orbitFor(const Vec3d& relVel, double radius = 400) {
    return makeOrbit({1000, 0, 0}, relVel, {0, 0, -1}, bodyMu(radius, 60));
}
}

TEST(guide_circle_has_the_right_shape) {
    Orbit o = orbitFor({0, 0, -84});
    std::vector<Vec3d> pts;
    Vec3d centre{100, 200, 300};
    circlePoints(centre, o, 96, pts);
    CHECK_EQ(pts.size(), 97u);
    for (auto& p : pts) {
        Vec3d d = sub(p, centre);
        CHECK(near(length(d), o.radius, 1e-6 * o.radius));           // radius kept
        CHECK(near(dot(d, o.normal), 0.0, 1e-6 * o.radius));         // in the plane
    }
    CHECK(near(length(sub(pts.front(), pts.back())), 0.0, 1e-6 * o.radius));   // closed
    CHECK(near(length(sub(pts.front(), add(centre, o.rel0))), 0.0, 1e-9));     // starts at the ship
    Vec3d rel = o.rel0;                                                  // orthonormal basis of the plane: rel, tangent, normal
    Vec3d t = orbitTangent(o.normal, rel);
    CHECK(near(length(t), 1.0, 1e-9));
    CHECK(near(dot(t, normalized(rel)), 0.0, 1e-9));
    CHECK(near(dot(t, o.normal), 0.0, 1e-9));
    CHECK(near(length(o.normal), 1.0, 1e-9));
}

TEST(guide_plane_degenerate_cases) {
    // no relative velocity: falls back to the ship's heading
    Orbit still = makeOrbit({1000, 0, 0}, {0, 0, 0}, {0, 0, -1}, bodyMu(400, 60));
    CHECK(near(length(still.normal), 1.0, 1e-9));
    CHECK(near(dot(still.normal, {1, 0, 0}), 0.0, 1e-9));
    // straight at the body: the velocity gives no plane, the heading does
    Orbit radial = makeOrbit({1000, 0, 0}, {-50, 0, 0}, {0, 0, -1}, bodyMu(400, 60));
    CHECK(near(length(radial.normal), 1.0, 1e-9));
    CHECK(near(dot(radial.normal, {1, 0, 0}), 0.0, 1e-9));
    // all degenerate at once (heading also radial): still a valid unit normal, no NaN
    Orbit worst = makeOrbit({1000, 0, 0}, {0, 0, 0}, {1, 0, 0}, bodyMu(400, 60));
    CHECK(near(length(worst.normal), 1.0, 1e-9));
    CHECK(worst.speed > 0 && std::isfinite(worst.omega));
    std::vector<Vec3d> pts;
    circlePoints({0, 0, 0}, worst, 48, pts);
    for (auto& p : pts) CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
}

TEST(guide_arc_ahead_follows_the_direction_of_travel_both_ways) {
    for (double dir : {1.0, -1.0}) {                                     // prograde and retrograde
        Vec3d v{0, 0, -84 * dir};
        Orbit o = orbitFor(v);
        std::vector<Vec3d> pts;
        circlePoints({0, 0, 0}, o, 96, pts);
        Vec3d step = sub(pts[1], pts[0]);
        CHECK(dot(step, v) > 0);                                         // the first segment heads the way the ship is going
        int ahead = arcAheadSegments(96, 90);
        CHECK_EQ(ahead, 24);
        // a quarter turn ahead is 90 degrees around
        Vec3d a = normalized(pts[0]), b = normalized(pts[(size_t)ahead]);
        CHECK(near(std::acos(dot(a, b)) * 180.0 / kPi, 90.0, 1e-6));
    }
    CHECK(isRetrograde(orbitFor({0, 0, 84}).normal) != isRetrograde(orbitFor({0, 0, -84}).normal));
    CHECK_EQ(arcAheadSegments(48, 90), 12);
    CHECK_EQ(arcAheadSegments(3, 1), 1);
    CHECK_EQ(arcAheadSegments(96, 720), 96);
}

TEST(guide_alignment_speed_edges) {
    Orbit o = orbitFor({0, 0, -84});
    double need = o.speed;
    Tolerances tol;                                                      // 12 %, 10 degrees
    auto check = [&](double speed) { Vec3d v{0, 0, -speed}; return checkAlignment(orbitFor(v), {1000, 0, 0}, v, 600, 400, 3, 20, tol); };
    CHECK(check(need).aligned());
    CHECK(check(need * 1.11).aligned());
    CHECK(check(need * 0.89).aligned());
    CHECK(check(need * 1.13).reason == Refusal::TooFast);
    CHECK(check(need * 0.87).reason == Refusal::TooSlow);
    CHECK(check(need * 1.13).speedError > 0 && check(need * 0.87).speedError < 0);
    Alignment a = check(need * 1.5);
    CHECK(near(a.needSpeed, need, 1e-9));
    CHECK(refusalText(a).find("too fast") != std::string::npos);
    CHECK(refusalText(a).find("need") != std::string::npos);
    CHECK(refusalText(check(need * 0.5)).find("too slow") != std::string::npos);
}

TEST(guide_alignment_heading_edges) {
    Orbit ref = orbitFor({0, 0, -84});
    double need = ref.speed;
    Tolerances tol;
    auto check = [&](double deg) {
        double r = deg * kPi / 180.0;
        Vec3d v{-need * std::sin(r), 0, -need * std::cos(r)};            // tilted from the tangent (-z) towards the body (-x)
        return checkAlignment(orbitFor(v), {1000, 0, 0}, v, 600, 400, 3, 20, tol);
    };
    CHECK(check(0).aligned());
    CHECK(check(9.5).aligned());
    CHECK(check(10.5).reason == Refusal::Heading);
    CHECK(near(check(34).headingDeg, 34.0, 1e-6));
    CHECK(refusalText(check(34)).find("34 degrees") != std::string::npos);
    CHECK(refusalText(check(34)).find("tangent") != std::string::npos);
    // flying straight in or out: heading is 90 and it is refused for the heading (not silently accepted)
    Vec3d in{-need, 0, 0};
    Alignment radial = checkAlignment(orbitFor(in), {1000, 0, 0}, in, 600, 400, 3, 20, tol);
    CHECK(radial.reason == Refusal::Heading);
    CHECK(radial.headingDeg > 80.0);
    // not moving at all
    Alignment still = checkAlignment(orbitFor({0, 0, 0}), {1000, 0, 0}, {0, 0, 0}, 600, 400, 3, 20, tol);
    CHECK(still.reason == Refusal::Heading);
    CHECK(near(still.headingDeg, 90.0, 1e-9));
    // retrograde is fine as long as it is tangential
    Vec3d back{0, 0, need};
    CHECK(checkAlignment(orbitFor(back), {1000, 0, 0}, back, 600, 400, 3, 20, tol).aligned());
}

TEST(guide_alignment_altitude_band_and_tolerances_are_configurable) {
    Vec3d v{0, 0, -84};
    Orbit o = orbitFor(v);
    Tolerances tol;
    CHECK(checkAlignment(o, {1000, 0, 0}, v, 10, 400, 3, 20, tol).reason == Refusal::TooClose);
    CHECK(checkAlignment(o, {1000, 0, 0}, v, 1300, 400, 3, 20, tol).reason == Refusal::TooFar);
    Tolerances tight{0.01, 1.0};
    CHECK(checkAlignment(o, {1000, 0, 0}, {0, 0, -84.0 * 1.05}, 600, 400, 3, 20, tight).reason != Refusal::None);
    CHECK(refusalText(checkAlignment(o, {1000, 0, 0}, v, 10, 400, 3, 20, tol)).find("close") != std::string::npos);
    CHECK(refusalText(checkAlignment(o, {1000, 0, 0}, v, 1300, 400, 3, 20, tol)).find("far") != std::string::npos);
}

TEST(guide_hud_text) {
    Alignment a; a.altitude = 1040; a.needSpeed = 84.4; a.speedError = 12.2; a.headingDeg = 4.1;
    CHECK_EQ(guideLine1("Planet 1", a), std::string("ORBIT PLANET 1  alt 1.0K  need 84 m/s"));
    CHECK_EQ(guideLine2(a), std::string("SPEED +12  HEADING 4 deg  [ALIGNED - press O]"));
    a.reason = Refusal::TooFast; a.speedError = 23.0;
    CHECK(guideLine2(a).find("[speed +23 m/s too fast (need 84)]") != std::string::npos);
    CHECK_EQ(altitudeText(420), std::string("420"));
}

TEST(guide_settle_is_smooth_and_ends_on_the_orbit) {
    Vec3d relVel{0, 0, -80};                                             // slower than the circular speed, within tolerance
    Orbit o = orbitFor(relVel);
    const double dt = 1.0 / 60, T = 1.5;
    Vec3d off = o.rel0;
    double prevErr = speedErrorVsCircular(o, off, relVel), startErr = prevErr;
    CHECK(startErr > 1.0);                                               // there is something to correct
    double t = 0;
    Vec3d prev = off;
    for (; t < T - 1e-9; t += dt) {
        Vec3d next = settleStep(o, relVel, off, t, dt, T);
        CHECK(std::isfinite(next.x) && std::isfinite(next.y) && std::isfinite(next.z));
        CHECK(length(sub(next, off)) < 2.0 * o.speed * dt);              // never faster than ~2x the orbital speed per step
        // the commanded velocity's error against the circular orbit only ever shrinks
        Vec3d v = mul(sub(next, off), 1.0 / dt);
        double err = speedErrorVsCircular(o, off, v);
        CHECK(err < startErr * 1.05);
        off = next;
    }
    CHECK(near(length(off), o.radius, 1e-6 * o.radius));                 // ends exactly on the circle
    // and the last commanded velocity is the circular one
    Vec3d last = settleStep(o, relVel, off, T - dt, dt, T);
    CHECK(speedErrorVsCircular(o, off, mul(sub(last, off), 1.0 / dt)) < 0.5);
    // after the hand-over the analytic orbit (re-anchored at the ship) continues without a jump
    Orbit re = o;
    re.rel0 = rotateAbout(off, o.normal, -o.omega * T);
    CHECK(near(length(sub(offsetAt(re, T), off)), 0.0, 1e-6 * o.radius));
    CHECK(near(length(re.rel0), o.radius, 1e-6 * o.radius));
    // seconds <= 0 is the old snap: one step is already on the circle
    Vec3d snap = settleStep(o, relVel, o.rel0, 0.0, dt, 0.0);
    CHECK(near(length(snap), o.radius, 1e-6 * o.radius));
}

TEST(guide_settle_weight_is_monotonic_and_speed_error_shrinks) {
    double last = -1;
    for (double x = -0.5; x <= 1.5; x += 0.05) { double s = smoothstep01(x); CHECK(s >= last - 1e-12); CHECK(s >= 0.0 && s <= 1.0); last = s; }
    CHECK(near(smoothstep01(0), 0.0) && near(smoothstep01(1), 1.0) && near(smoothstep01(0.5), 0.5));
    // a ship already flying the circular orbit has no error and stays on it
    Orbit o = orbitFor({0, 0, -84});
    Vec3d exact = mul(cross(o.normal, o.rel0), o.omega);
    CHECK(speedErrorVsCircular(o, o.rel0, exact) < 1e-6);
    CHECK(speedErrorVsCircular(o, o.rel0, add(exact, {5, 0, 0})) > 4.9);
    Vec3d off = o.rel0;
    for (double t = 0; t < 1.5; t += 1.0 / 60) {
        Vec3d n = settleStep(o, exact, off, t, 1.0 / 60, 1.5);
        CHECK(near(length(n), o.radius, 1e-3 * o.radius));
        off = n;
    }
}
