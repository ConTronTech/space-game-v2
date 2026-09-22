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
    Tolerances tol;                                                      // 25 %, 25 degrees (3.5d: was 12 %, 10 degrees)
    CHECK(near(tol.speedFrac, 0.25) && near(tol.angleDeg, 25.0));
    auto check = [&](double speed) { Vec3d v{0, 0, -speed}; return checkAlignment(orbitFor(v), {1000, 0, 0}, v, 600, 400, 3, 20, tol); };
    CHECK(check(need).aligned());
    CHECK(check(need * 1.24).aligned());
    CHECK(check(need * 0.76).aligned());
    CHECK(check(need * 1.26).reason == Refusal::TooFast);
    CHECK(check(need * 0.74).reason == Refusal::TooSlow);
    CHECK(check(need * 1.26).speedError > 0 && check(need * 0.74).speedError < 0);
    Alignment a = check(need * 1.5);
    CHECK(near(a.needSpeed, need, 1e-9));
    CHECK(refusalText(a).find("too fast") != std::string::npos);
    CHECK(refusalText(a).find("too fast: slow to ") != std::string::npos);          // says what to do
    CHECK(refusalText(check(need * 0.5)).find("too slow: speed up to ") != std::string::npos);
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
    CHECK(check(20).aligned());                                          // a forgiving 20-degree error is accepted
    CHECK(check(24.5).aligned());
    CHECK(check(25.5).reason == Refusal::Heading);
    CHECK(near(check(34).headingDeg, 34.0, 1e-6));
    CHECK_EQ(refusalText(check(34)), std::string("turn 34 deg toward the arc ahead"));
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
    CHECK_EQ(refusalText(checkAlignment(o, {1000, 0, 0}, v, 10, 400, 3, 20, tol)), std::string("too close to the surface: climb above 20"));
    CHECK_EQ(refusalText(checkAlignment(o, {1000, 0, 0}, v, 1300, 400, 3, 20, tol)), std::string("too far: fly within 1.2K of the surface"));
}

TEST(guide_hud_text) {
    Alignment a; a.altitude = 1040; a.needSpeed = 84.4; a.speedError = 12.2; a.headingDeg = 4.1;
    CHECK_EQ(guideLine1("Planet 1", a), std::string("ORBIT PLANET 1  alt 1.0K  need 84 m/s"));
    CHECK_EQ(guideLine2(a), std::string("SPEED +12  HEADING 4 deg  [ALIGNED - press O]"));
    a.reason = Refusal::TooFast; a.speedError = 23.0;
    CHECK(guideLine2(a).find("[23 m/s too fast: slow to 84 m/s]") != std::string::npos);
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
    CHECK(near(length(off), o.radius, 0.01 * o.radius));                 // ends on a circle close to the lock radius (a tangential entry: no drift in or out)
    // and the last commanded velocity is the circular one
    Vec3d last = settleStep(o, relVel, off, T - dt, dt, T);
    CHECK(speedErrorVsCircular(o, off, mul(sub(last, off), 1.0 / dt)) < 0.5);
    // after the hand-over the analytic orbit (re-anchored at the ship) continues without a jump
    Orbit re = rebaseOrbit(o, off, T);
    CHECK(near(length(sub(offsetAt(re, T), off)), 0.0, 1e-6 * o.radius));
    CHECK(near(re.radius, length(off), 1e-9));
    CHECK(near(re.speed, circularSpeed(orbitMu(o), re.radius), 1e-9));
    CHECK(length(sub(mul(sub(offsetAt(re, T + dt), off), 1.0 / dt), mul(sub(last, off), 1.0 / dt))) < 0.5);
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

// ---- 3.5d: radial approach plane, hysteresis, visibility, settle ----
namespace {
// a velocity `deg` degrees away from straight at the body (-x), rest along -z (the tangent)
Vec3d nearRadial(double deg, double speed = 60) { double r = deg * kPi / 180.0; return {-speed * std::cos(r), 0, -speed * std::sin(r)}; }
}

TEST(guide_plane_fallback_for_radial_and_near_radial_velocity) {
    const Vec3d rel{1000, 0, 0};
    for (double deg : {0.0, 2.0, 8.0}) {                                 // straight at the body, and nearly
        bool fb = false;
        Vec3d n = guidePlaneNormal(rel, nearRadial(deg), {-1, 0, 0}, fb);   // nose at the body too
        CHECK(fb);
        CHECK(near(length(n), 1.0, 1e-9));
        CHECK(near(dot(n, normalized(rel)), 0.0, 1e-9));                 // the plane contains the ship's offset: the ring passes through the ship
        // not the (noise-set) plane of the velocity: the nose is radial, so the plane holds the ship's RIGHT vector (+z for nose -x, up +y)
        Vec3d t = orbitTangent(n, rel);
        CHECK(std::fabs(dot(t, {0, 0, 1})) > 0.99);
        std::vector<Vec3d> pts;                                          // never a degenerate ring: a real circle of the orbit radius
        Orbit o = makeOrbitWithNormal(rel, n, bodyMu(400, 60));
        circlePoints({0, 0, 0}, o, 48, pts);
        for (auto& p : pts) CHECK(near(length(p), 1000.0, 1e-6));
        CHECK(length(sub(pts[12], pts[36])) > 1999.0);                   // opposite points a diameter apart (not collapsed onto a line through the centre)
    }
    // nose across the radius: the plane follows the nose
    bool fb = false;
    Vec3d n = guidePlaneNormal(rel, nearRadial(0), {0, 1, 0}, fb);
    CHECK(fb && std::fabs(dot(orbitTangent(n, rel), {0, 1, 0})) > 0.99);
    // clearly tangential: the plane of the velocity, no fallback, tangent = the tangential part of the velocity
    fb = false;
    n = guidePlaneNormal(rel, nearRadial(60), {-1, 0, 0}, fb);
    CHECK(!fb);
    CHECK(dot(orbitTangent(n, rel), {0, 0, -1}) > 0.999);
    // the tangent marker and the arc ahead agree (both rotate the same way about the same normal)
    Orbit o = makeOrbitWithNormal(rel, n, bodyMu(400, 60));
    std::vector<Vec3d> pts;
    circlePoints({0, 0, 0}, o, 96, pts);
    CHECK(dot(normalized(sub(pts[1], pts[0])), orbitTangent(o.normal, o.rel0)) > 0.99);
    // no motion at all: fallback, still a valid plane
    fb = false;
    CHECK(near(length(guidePlaneNormal(rel, {0, 0, 0}, {-1, 0, 0}, fb)), 1.0, 1e-9) && fb);
}

TEST(guide_plane_fallback_has_hysteresis) {
    const Vec3d rel{1000, 0, 0};
    bool fb = false;
    guidePlaneNormal(rel, nearRadial(5), {-1, 0, 0}, fb);                // tangential fraction ~0.09: enter
    CHECK(fb);
    for (double deg : {12.0, 14.0, 16.0, 12.0, 17.0}) {                  // 0.21 .. 0.29: between enter and leave, no flip
        guidePlaneNormal(rel, nearRadial(deg), {-1, 0, 0}, fb);
        CHECK(fb);
    }
    guidePlaneNormal(rel, nearRadial(20), {-1, 0, 0}, fb);               // 0.34: leave
    CHECK(!fb);
    for (double deg : {16.0, 13.0, 12.5}) {                              // back into the band from above: stays on the velocity plane
        guidePlaneNormal(rel, nearRadial(deg), {-1, 0, 0}, fb);
        CHECK(!fb);
    }
    guidePlaneNormal(rel, nearRadial(10), {-1, 0, 0}, fb);               // 0.17: enter again
    CHECK(fb);
    // a slow drift (tangential part under 2 m/s) is noise, whatever the fraction
    fb = false;
    guidePlaneNormal(rel, {0, 0, -1.5}, {-1, 0, 0}, fb);
    CHECK(fb);
}

TEST(guide_aligned_state_has_hysteresis) {
    Tolerances tol;
    const double need = orbitFor({0, 0, -84}).speed;
    auto both = [&](double deg, Alignment& strict, Alignment& wide) {
        double r = deg * kPi / 180.0;
        Vec3d v{-need * std::sin(r), 0, -need * std::cos(r)};
        Orbit o = orbitFor(v);
        strict = checkAlignment(o, {1000, 0, 0}, v, 600, 400, 3, 20, tol);
        wide = checkAlignment(o, {1000, 0, 0}, v, 600, 400, 3, 20, widened(tol));
    };
    Alignment s, w;
    AlignLatch l;
    const double dt = 1.0 / 60;
    both(26, s, w);
    CHECK(!updateAlignLatch(l, s, w, dt));                               // never aligned yet: 26 is out
    both(20, s, w);
    CHECK(updateAlignLatch(l, s, w, dt));
    for (int i = 0; i < 120; i++) {                                      // hovering at the edge (24.5 / 26 / 30 deg, all inside 25 x 1.25): stays aligned
        both(i % 3 == 0 ? 24.5 : (i % 3 == 1 ? 26.0 : 30.0), s, w);
        CHECK(updateAlignLatch(l, s, w, dt));
    }
    both(33, s, w);                                                      // beyond the widened tolerance: held for 0.3 s, then dropped
    double t = 0;
    while (updateAlignLatch(l, s, w, dt)) { t += dt; CHECK(t < 1.0); if (t >= 1.0) break; }
    CHECK(t > 0.28 && t < 0.33);
    CHECK(!updateAlignLatch(l, s, w, dt));
    both(30, s, w);                                                      // after dropping, the strict tolerance is needed again
    CHECK(!updateAlignLatch(l, s, w, dt));
    // a short excursion beyond the wide tolerance does not drop it
    both(10, s, w); CHECK(updateAlignLatch(l, s, w, dt));
    both(40, s, w); for (int i = 0; i < 10; i++) CHECK(updateAlignLatch(l, s, w, dt));
    both(28, s, w); CHECK(updateAlignLatch(l, s, w, dt));
    CHECK(near(l.outFor, 0.0));
    // leaving the altitude band drops it at once
    Vec3d v{0, 0, -need};
    Alignment far = checkAlignment(orbitFor(v), {1000, 0, 0}, v, 1300, 400, 3, 20, tol);
    CHECK(!updateAlignLatch(l, far, far, dt));
}

TEST(guide_visibility_conditions) {
    // in range (6 radii above the surface of a 400 planet = 2400, but at least 2500): no key press, no speed needed
    CHECK(near(guideRange(400, 6, 2500), 2500.0));
    CHECK(near(guideRange(1800, 6, 2500), 10800.0));
    CHECK(near(guideRange(50, 6, 2500), 2500.0));
    CHECK(guideVisible(true, false, true, false, false, 1000, 400, 6, 2500));
    CHECK(guideVisible(true, false, true, false, false, 2500, 400, 6, 2500));
    CHECK(!guideVisible(true, false, true, false, false, 2501, 400, 6, 2500));
    CHECK(guideVisible(true, false, true, false, false, 0, 400, 6, 2500));
    CHECK(!guideVisible(true, false, true, false, false, -1, 400, 6, 2500));   // inside the body
    CHECK(!guideVisible(false, false, true, false, false, 1000, 400, 6, 2500));  // orbit.show_guide off
    CHECK(!guideVisible(true, true, true, false, false, 1000, 400, 6, 2500));    // locked
    CHECK(!guideVisible(true, false, false, false, false, 1000, 400, 6, 2500));  // dead
    CHECK(!guideVisible(true, false, true, true, false, 1000, 400, 6, 2500));    // warping
    CHECK(!guideVisible(true, false, true, false, true, 1000, 400, 6, 2500));    // docking
}

TEST(guide_settle_from_a_wide_error_is_a_smooth_curve) {
    Tolerances tol;
    const double need = orbitFor({0, 0, -84}).speed;
    double r = 22 * kPi / 180.0;
    Vec3d relVel{-need * 1.15 * std::sin(r), 0, -need * 1.15 * std::cos(r)};   // 22 degrees off and 15 % fast: accepted now
    Orbit o = orbitFor(relVel);
    Alignment a = checkAlignment(o, {1000, 0, 0}, relVel, 600, 400, 3, 20, tol);
    CHECK(a.aligned());
    double T = settleSecondsFor(1.5, a, tol);
    CHECK(T > 2.5 && T <= 3.0);                                          // bigger error, longer settle
    CHECK(near(settleSecondsFor(1.5, Alignment{}, tol), 1.5));
    CHECK(near(settleSecondsFor(0.0, a, tol), 0.0));                     // 0 stays a snap
    CHECK(near(settleSecondsFor(4.0, a, tol), 4.0));                     // never shorter than asked
    const double dt = 1.0 / 60;
    Vec3d off = o.rel0, vPrev = relVel;
    double dv0 = speedErrorVsCircular(o, off, relVel), maxAccel = 0, prevRadErr = 1e9;
    for (double t = 0; t < T - 1e-9; t += dt) {
        Vec3d next = settleStep(o, relVel, off, t, dt, T);
        Vec3d v = mul(sub(next, off), 1.0 / dt);
        maxAccel = std::max(maxAccel, length(sub(v, vPrev)) / dt);
        vPrev = v;
        off = next;
        double radial = dot(v, normalized(off));                         // the entry's inward speed fades smoothly to 0
        CHECK(std::fabs(radial) <= prevRadErr + 0.05);
        prevRadErr = std::fabs(radial);
        CHECK(length(off) > 0.9 * o.radius);                             // a 22-degree inward entry only sinks a little (not pulled into the body)
    }
    CHECK(prevRadErr < 0.5);
    Vec3d vEnd = mul(sub(settleStep(o, relVel, off, T, dt, T), off), 1.0 / dt);
    CHECK(speedErrorVsCircular(o, off, vEnd) < 0.05);                    // on a circular orbit at the end
    // correction acceleration: the blend peaks at 1.5 x error / T, plus the circular motion's own o.speed^2 / radius (and the radius easing)
    CHECK(maxAccel < 1.5 * dv0 / T + o.speed * o.speed / o.radius + 12.0);
    CHECK(maxAccel < 20.0);                                              // a curve, not a snap (a one-step snap would be ~dv0 / dt = 2000+)
    // the floor: an entry diving at the body never goes below minRadius
    Vec3d dive = nearRadial(40, 150);
    Orbit od = orbitFor(dive);
    off = od.rel0;
    for (double t = 0; t < 3.0 - 1e-9; t += dt) { off = settleStep(od, dive, off, t, dt, 3.0, 980); CHECK(length(off) >= 980 - 1e-6); }
}

TEST(guide_settle_is_continuous_when_the_guide_plane_changes) {
    // the plane flips from the fallback to the velocity plane between two frames; the lock freezes the orbit at the key press, so the settle
    // that follows is continuous whichever plane was chosen: from the ship's offset, no jump in the first step
    const Vec3d rel{1000, 0, 0};
    for (double deg : {14.0, 22.0}) {
        for (bool startFb : {false, true}) {
            bool fb = startFb;
            Vec3d v = nearRadial(deg, 84);
            Orbit o = makeOrbit(rel, v, {-1, 0, 0}, bodyMu(400, 60), fb);
            const double dt = 1.0 / 60, T = 3.0;
            Vec3d off = o.rel0, vPrev = v;
            for (double t = 0; t < T - 1e-9; t += dt) {
                Vec3d next = settleStep(o, v, off, t, dt, T);
                Vec3d vel = mul(sub(next, off), 1.0 / dt);
                CHECK(length(sub(vel, vPrev)) < 1.5);                    // per-step velocity change: no snap
                vPrev = vel;
                off = next;
            }
            CHECK(std::isfinite(length(off)) && length(off) > 0.8 * o.radius);
        }
    }
}
