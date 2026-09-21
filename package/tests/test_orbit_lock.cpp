#include <cmath>
#include "ship/orbit_lock/orbit_lock_rules.h"
#include "tests/test.h"

namespace {
using orbit::Vec3d;
bool close(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol * std::max(1.0, std::fabs(b)); }
bool closeV(const Vec3d& a, const Vec3d& b, double tol = 1e-6) { return orbit::length(orbit::sub(a, b)) <= tol * std::max(1.0, orbit::length(b)); }
} // namespace

TEST(orbit_speed_and_rate_follow_the_gravity_model) {
    double mu = orbit::bodyMu(400.0, 60.0);
    CHECK(close(mu, 60.0 * 160000.0));
    CHECK(close(orbit::circularSpeed(mu, 1000.0), std::sqrt(mu / 1000.0)));
    CHECK(close(orbit::circularSpeed(mu, 1000.0), 97.98, 1e-3));            // ~98 m/s, the documented example
    double v = orbit::circularSpeed(mu, 1000.0);
    CHECK(close(v * v * 1000.0, mu));                                        // v^2 r = mu
    CHECK(close(orbit::angularRate(v, 1000.0), v / 1000.0));
    CHECK(close(orbit::circularSpeed(mu, 0.0), 0.0));                        // degenerate: no NaN
    CHECK(close(orbit::circularSpeed(0.0, 100.0), 0.0));
    CHECK(close(orbit::angularRate(5.0, 0.0), 0.0));
    CHECK(orbit::circularSpeed(orbit::bodyMu(800, 60), 1000) > orbit::circularSpeed(orbit::bodyMu(400, 60), 1000));   // bigger body, faster
}

TEST(orbit_nearest_body_is_by_surface_not_centre) {
    std::vector<orbit::Candidate> b = {{{0, 0, 0}, 1000.0}, {{2500, 0, 0}, 50.0}};      // huge planet at 0, small moon at 2500
    auto n = orbit::nearestBody(b, {2300, 0, 0});           // 1300 above the planet surface, 150 above the moon's
    CHECK_EQ(n.index, 1); CHECK(close(n.altitude, 150.0));
    n = orbit::nearestBody(b, {1500, 0, 0});                // 500 above the planet, 950 above the moon
    CHECK_EQ(n.index, 0); CHECK(close(n.altitude, 500.0));
    n = orbit::nearestBody(b, {500, 0, 0});                 // inside the planet: negative altitude
    CHECK_EQ(n.index, 0); CHECK(n.altitude < 0);
    CHECK_EQ(orbit::nearestBody({}, {0, 0, 0}).index, -1);
}

TEST(orbit_engage_range) {
    CHECK(orbit::inEngageRange(1000.0, 400.0, 3.0, 20.0));       // 1000 <= 1200
    CHECK(orbit::inEngageRange(1200.0, 400.0, 3.0, 20.0));       // edge
    CHECK(!orbit::inEngageRange(1200.5, 400.0, 3.0, 20.0));      // too far
    CHECK(!orbit::inEngageRange(10.0, 400.0, 3.0, 20.0));        // too close to the surface
    CHECK(!orbit::inEngageRange(-5.0, 400.0, 3.0, 20.0));        // inside
}

TEST(orbit_plane_normal_follows_motion_with_fallbacks) {
    Vec3d rel{1000, 0, 0};
    Vec3d n = orbit::planeNormal(rel, {0, 0, -50}, {1, 0, 0});           // moving -z: right-hand normal of (x, -z) is +y? check the sense
    Vec3d moved = orbit::rotateAbout(rel, n, 0.01);                       // a small step about n goes the way the ship was moving
    CHECK(moved.z < 0);
    n = orbit::planeNormal(rel, {0, 0, 50}, {1, 0, 0});                   // opposite motion, opposite sense
    CHECK(orbit::rotateAbout(rel, n, 0.01).z > 0);
    CHECK(close(orbit::length(n), 1.0));
    CHECK(close(orbit::dot(n, rel), 0.0));                                // normal is perpendicular to the offset
    // degenerate: no velocity -> heading; heading parallel to rel -> world up; everything degenerate still gives a unit normal
    Vec3d a = orbit::planeNormal(rel, {0, 0, 0}, {0, 0, -1});
    CHECK(orbit::rotateAbout(rel, a, 0.01).z < 0);
    Vec3d b = orbit::planeNormal(rel, {0, 0, 0}, {1, 0, 0});
    CHECK(close(orbit::length(b), 1.0)); CHECK(close(orbit::dot(b, rel), 0.0));
    Vec3d c = orbit::planeNormal({0, 1000, 0}, {0, 30, 0}, {0, 1, 0});   // moving straight away, heading the same way, rel along up
    CHECK(close(orbit::length(c), 1.0)); CHECK(close(orbit::dot(c, {0, 1000, 0}), 0.0));
    Vec3d z = orbit::planeNormal({0, 0, 0}, {0, 0, 0}, {0, 0, 0});        // total garbage in: still a unit vector
    CHECK(close(orbit::length(z), 1.0));
}

TEST(orbit_position_is_on_the_circle_and_periodic) {
    Vec3d rel{600, 300, -400};
    orbit::Orbit o = orbit::makeOrbit(rel, {-20, 40, 10}, {0, 0, -1}, orbit::bodyMu(400, 60));
    CHECK(close(o.radius, orbit::length(rel)));
    CHECK(close(orbit::length(orbit::offsetAt(o, 0.0)), o.radius));
    CHECK(closeV(orbit::offsetAt(o, 0.0), rel));
    double period = 2.0 * 3.14159265358979323846 / o.omega;
    for (double t : {1.0, 17.5, 123.4, 1e5, 1e7}) {
        Vec3d off = orbit::offsetAt(o, t);
        CHECK(close(orbit::length(off), o.radius, 1e-9));                  // stays on the circle, however long
        CHECK(close(orbit::dot(off, o.normal), orbit::dot(rel, o.normal), 1e-6));   // stays in the plane
    }
    CHECK(closeV(orbit::offsetAt(o, period), rel, 1e-6));                  // one period: back at the start
    // tangential speed matches the circular speed
    double dt = 1e-3;
    Vec3d step = orbit::sub(orbit::offsetAt(o, dt), orbit::offsetAt(o, 0));
    CHECK(close(orbit::length(step) / dt, o.speed, 1e-3));
    // follows a moving body: position = body + offset
    Vec3d body{1000, 2000, 3000};
    CHECK(closeV(orbit::positionOnOrbit(body, o, 10.0), orbit::add(body, orbit::offsetAt(o, 10.0))));
}

TEST(orbit_no_drift_over_a_long_lock) {
    // the same analytic call every step, for an hour of 60 Hz steps: radius error stays at rounding level
    orbit::Orbit o = orbit::makeOrbit({0, 0, 800}, {50, 0, 0}, {1, 0, 0}, orbit::bodyMu(300, 60));
    double worst = 0, t = 0;
    for (int i = 0; i < 216000; i++) { t += 1.0 / 60; worst = std::max(worst, std::fabs(orbit::length(orbit::offsetAt(o, t)) - o.radius)); }
    CHECK(worst < 1e-6);
}

TEST(orbit_release_rules) {
    CHECK(!orbit::shouldRelease(true, false, 0, 0, 0, false, true));       // idle: stays locked
    CHECK(orbit::shouldRelease(true, false, 0.5, 0, 0, false, true));      // thrust
    CHECK(orbit::shouldRelease(true, false, 0, -0.5, 0, false, true));     // strafe
    CHECK(orbit::shouldRelease(true, false, 0, 0, 0.5, false, true));      // lift
    CHECK(orbit::shouldRelease(true, false, 0, 0, 0, true, true));         // brake
    CHECK(!orbit::shouldRelease(true, false, 0.05, 0, 0, false, true));    // inside the dead zone
    CHECK(!orbit::shouldRelease(true, false, 1.0, 1.0, 1.0, true, false)); // tunable off: burning does not release
    CHECK(orbit::shouldRelease(false, false, 0, 0, 0, false, false));      // dead always releases
    CHECK(orbit::shouldRelease(true, true, 0, 0, 0, false, false));        // warping always releases
}
