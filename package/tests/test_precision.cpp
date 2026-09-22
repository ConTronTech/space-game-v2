// Double-precision ship / physics positions (docs/PRECISION.md, task 7.1).
// Two things are proved here: at TODAY's distances nothing changes (the double path tracks the old float path to well inside a
// millimetre), and far from the origin - where the realistic-scale rescale will put the ship - only the double path is still correct.
#include <cmath>
#include "core/camera/camera_math.h"
#include "core/physics_world/physics_world.h"
#include "engine/engine.h"
#include "engine/json.h"
#include "ship/gravity/gravity_rules.h"
#include "ship/orbit_lock/orbit_lock_rules.h"
#include "ship/ship_core/ship_rules.h"
#include "tests/test.h"
#include "world/stations/stations_rules.h"

using engine::Vec3;
using engine::Vec3d;

namespace {
namespace prec {

// The OLD ship_core integration, kept here as the reference to compare against: a float position accumulating float steps.
inline void integrateFloat(Vec3& pos, const Vec3& vel, float dt) { pos += vel * dt; }

inline double dist(const Vec3d& a, const Vec3d& b) {
    double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
inline Vec3d sub(const Vec3d& a, const Vec3d& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline double len(const Vec3d& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

// One float ULP at |x| (how far apart two neighbouring float values are there): 1 m at 1.7e7, 512 m at 4.4e9.
inline double ulp(double x) { return (double)std::nextafterf((float)x, 2e38f) - (double)(float)x; }

struct Flight {
    Vec3d posD;
    Vec3 posF;
    Vec3 vel;
    // fly n fixed steps of dt with a constant velocity, both ways at once
    void run(int n, float dt) {
        for (int i = 0; i < n; i++) {
            ship::rules::integrate(posD, vel, dt);
            integrateFloat(posF, vel, dt);
        }
    }
};

} // namespace prec
} // namespace

// ---- 1. today's scale: the change is invisible ----

TEST(precision_flight_at_todays_scale_is_unchanged) {
    // 12,000 units out (the sun's distance from the spawn point: about as far as anything gets today except the outer planets)
    const double start = 12000.25;
    const float dt = 1.0f / 60.0f;
    const double flown = 36000 * (double)dt;                        // ten minutes, counted in the dt the game actually uses
    prec::Flight f{{start, -start * 0.5, start * 0.25}, {(float)start, (float)(-start * 0.5), (float)(start * 0.25)}, {37.5f, -12.25f, 3.0f}};
    f.run(36000, dt);
    const Vec3d exact{start + 37.5 * flown, -start * 0.5 - 12.25 * flown, start * 0.25 + 3.0 * flown};
    const double errD = prec::dist(f.posD, exact);
    const double errF = prec::dist({f.posF.x, f.posF.y, f.posF.z}, exact);
    // Measured on this machine: errD = 0.0 exactly, errF = 2.89 m. The old float position drifted ~3 m away from the truth over ten
    // minutes of unbroken thrust even HERE; the double one lands on it. Nothing in the game reads absolute position to that accuracy
    // (every distance, dock hold and collision is re-derived from the current positions each step), so play at this scale is unchanged -
    // the ship simply no longer accumulates its own rounding.
    CHECK(errD < 1e-6);
    CHECK(errF > 0.5);                                              // the old path's accumulated drift, for the record
    CHECK(prec::dist(f.posD, {f.posF.x, f.posF.y, f.posF.z}) < 5.0);
}

TEST(precision_flight_near_the_origin_is_unchanged) {
    const float dt = 1.0f / 60.0f;
    prec::Flight f{{0, 0, 0}, {0, 0, 0}, {0, 0, -40.0f}};           // the spawn point, full thrust forward
    f.run(600, dt);
    CHECK(std::abs(f.posD.z + 40.0 * 600 * (double)dt) < 1e-9);     // ten seconds of 40 m/s
    CHECK(std::abs(f.posD.z + 400.0) < 1e-3);                       // ... which is 400 m to within the float dt's own rounding
    CHECK(prec::dist(f.posD, {f.posF.x, f.posF.y, f.posF.z}) < 5e-3);   // measured 1.5 mm after ten seconds: the same flight
}

// ---- 2. far from the origin: the float position quantises, the double one does not ----

TEST(precision_flight_far_out_float_quantises_double_does_not) {
    const double start = 1.0e7;                                     // ten million units: the task's minimum proof distance
    const float dt = 1.0f / 60.0f;
    const double travel = 100.0 * 600 * (double)dt;                 // ten seconds at 100 m/s = 1000 m (to the float dt's own precision)
    prec::Flight f{{start, 0, 0}, {(float)start, 0, 0}, {100.0f, 0, 0}};   // the step is 1.667 m per frame at 60 Hz
    f.run(600, dt);
    const double expected = start + travel;
    CHECK(std::abs(f.posD.x - expected) < 1e-6);                    // double: the exact distance
    CHECK(std::abs((double)f.posF.x - expected) > 100.0);           // float: measured 1200 m travelled instead of 1000 - 20% too far
    CHECK(prec::ulp(start) > 0.5);

    const double far = 5.0e9;                                       // Neptune-orbit scale: one float ULP is 512 m
    prec::Flight g{{far, 0, 0}, {(float)far, 0, 0}, {100.0f, 0, 0}};
    g.run(600, dt);
    CHECK(std::abs(g.posD.x - (far + travel)) < 1e-3);              // double: measured 0.14 mm of accumulated error over 600 steps
    CHECK((double)g.posF.x == (double)(float)far);                  // float: 10 s of thrust moved the ship NOWHERE
    CHECK(prec::ulp(far) > 400.0);
}

TEST(precision_flight_far_out_has_no_per_frame_snapping) {
    // What a pilot sees: the per-frame position delta must equal velocity * dt every frame, not 0, 0, 0, 512.
    const double far = 5.0e9;
    Vec3d p{far, 0, 0};
    Vec3 pf{(float)far, 0, 0}, vel{100.0f, 0, 0};
    const float dt = 1.0f / 60.0f;
    const double step = 100.0 * (double)dt;
    double worstD = 0;
    bool floatEverSnapped = false, floatEverStood = false;
    for (int i = 0; i < 600; i++) {
        Vec3d before = p;
        float beforeF = pf.x;
        ship::rules::integrate(p, vel, dt);
        pf += vel * dt;
        worstD = std::max(worstD, std::abs(prec::len(prec::sub(p, before)) - step));
        double deltaF = (double)pf.x - (double)beforeF;
        if (deltaF == 0.0) floatEverStood = true;
        if (deltaF > step * 2) floatEverSnapped = true;
    }
    CHECK(worstD < 1e-6);                                           // double: every frame moves exactly velocity * dt
    CHECK(floatEverStood);                                          // float: frames where the ship did not move at all
    CHECK(!floatEverSnapped || floatEverStood);                     // (and, when it moves, it jumps a whole ULP)
}

// ---- 3. the physics world: a metre-scale contact test at billions of units ----

namespace {
namespace prec {
struct FarPhysRig {
    engine::Engine eng;
    core::PhysicsWorld w;
    std::vector<core::Collided> hits;
    FarPhysRig() {
        w.setCellSize(50.0f);
        w.init(eng);
        eng.events.subscribe<core::Collided>([this](const core::Collided& c) { hits.push_back(c); });
    }
};
} // namespace prec
} // namespace

TEST(precision_physics_contact_is_exact_far_from_the_origin) {
    prec::FarPhysRig r;
    const double far = 5.0e9;
    core::BodyId rock = r.w.addBody("asteroid", Vec3d{far + 10.0, far, far}, 3.0f, false);
    core::BodyId ship = r.w.addBody("ship", Vec3d{far, far, far}, 1.0f, true);
    CHECK(rock >= 0);
    r.w.step();
    CHECK_EQ((int)r.hits.size(), 0);                                // 10 apart, radii 4: no contact yet
    r.w.setBody(ship, Vec3d{far + 6.0, far, far}, Vec3{60.0f, 0, 0});   // sweep into it
    r.w.step();
    CHECK_EQ((int)r.hits.size(), 1);
    if (!r.hits.empty()) {
        const core::Collided& c = r.hits[0];
        // the contact happened exactly where the spheres touch: 4 units apart, with a 60 m/s closing speed
        CHECK(std::abs(prec::dist(c.posAd, c.posBd) - 4.0) < 1e-6);
        CHECK(std::abs((double)c.posAd.x - (far + 6.0)) < 1e-6);
        CHECK(std::abs((double)c.speed - 60.0) < 1e-3);
        CHECK(std::abs((double)c.normal.x - 1.0) < 1e-5);
        // the float mirror of the same contact point cannot hold the 6 units of approach at all: that is what the push-out used to use
        CHECK(std::abs((double)c.posA.x - c.posAd.x) > 1.0);
        CHECK((double)c.posA.x == far);                             // the whole 6 m sweep rounded away
    }
}

TEST(precision_physics_push_out_lands_on_the_surface_far_out) {
    // ship_core's bounce: pos = contact point of the other body, minus the normal times (hull + other radius). With a float contact
    // point that lands hundreds of metres inside or outside the rock at 5e9; in double it is exact.
    const double far = 5.0e9;
    const Vec3d other{far + 4.0, far, far};
    const Vec3 n{1, 0, 0};
    const float hull = 2.5f, otherR = 3.0f;
    const Vec3d fixed = ship::rules::offsetD(other, n * -(hull + otherR + 0.02f));
    CHECK(std::abs(prec::dist(fixed, other) - (double)(hull + otherR + 0.02f)) < 1e-6);
    const Vec3 otherF{(float)other.x, (float)other.y, (float)other.z};
    const Vec3 old = otherF - n * (hull + otherR + 0.02f);
    CHECK(std::abs(prec::dist({old.x, old.y, old.z}, other) - (double)(hull + otherR + 0.02f)) > 1.0);   // the old float push-out: metres off the surface
    CHECK(prec::ulp(far) > 400.0);                                  // (and up to 512 m off wherever the rock is not a round float)
}

// ---- 4. the steering the orbit lock and docking do: (target - position) / dt ----

TEST(precision_steering_velocity_far_out) {
    const double far = 5.0e9 + 123.456;                             // a position a float cannot hold (one ULP out there is 512 m)
    const Vec3d pos{far, far * 0.5 + 7.75, -far * 0.25 - 61.0};
    const Vec3d target{pos.x + 1.25, pos.y + 0.5, pos.z - 2.0};     // where the analytic orbit / the pad wants the ship next step
    const double dt = 1.0 / 60.0;

    const Vec3d vD{(target.x - pos.x) / dt, (target.y - pos.y) / dt, (target.z - pos.z) / dt};
    CHECK(std::abs(vD.x - 75.0) < 1e-6);                            // 1.25 m in 1/60 s = 75 m/s
    CHECK(std::abs(vD.z + 120.0) < 1e-6);

    // the old path: the ship's position arrived as a float
    const Vec3 posF{(float)pos.x, (float)pos.y, (float)pos.z};
    const Vec3d vF{(target.x - posF.x) / dt, (target.y - posF.y) / dt, (target.z - posF.z) / dt};
    CHECK(prec::len(prec::sub(vF, vD)) > 1000.0);                   // km/s of pure noise: the lock and the dock hold explode
}

TEST(precision_dock_hold_distance_far_out) {
    // docking logs (and holds) the distance between the ship and the pad point; both are absolute positions far from the origin.
    const double far = 4.5e9;
    const Vec3d pad{far, far, far};
    const Vec3d ship = ship::rules::offsetD(pad, Vec3{0, 0.5f, 0});  // held half a metre above the pad
    CHECK(std::abs(prec::dist(ship, pad) - 0.5) < 1e-6);
    const Vec3 shipF{(float)ship.x, (float)ship.y, (float)ship.z}, padF{(float)pad.x, (float)pad.y, (float)pad.z};
    CHECK(prec::dist({shipF.x, shipF.y, shipF.z}, {padF.x, padF.y, padF.z}) != 0.5);   // in float the hold cannot even be measured
}

// ---- 5. plumbing: the pose, the save file ----

TEST(precision_pose_carries_the_double_position) {
    core::Pose ship;
    ship.posD = {1.0e9 + 0.25, 2.0, 3.0};
    ship.pos = {(float)ship.posD.x, (float)ship.posD.y, (float)ship.posD.z};
    ship.fwd = {0, 0, -1}; ship.up = {0, 1, 0};
    core::Pose chase = core::cam::chasePose(ship, 27.0f, 7.0f);
    CHECK(std::abs(chase.posD.x - ship.posD.x) < 1e-9);             // the chase offset is along the ship's own axes...
    CHECK(std::abs(chase.posD.y - (ship.posD.y + 7.0)) < 1e-6);
    CHECK(std::abs(chase.posD.z - (ship.posD.z + 27.0)) < 1e-6);
    CHECK(std::abs(chase.posD.x - (double)chase.pos.x) > 0.01);     // ... and the double eye keeps the 0.25 the float one lost
}

TEST(precision_saved_position_round_trips_as_double) {
    const Vec3d pos{4.512345678901234e9, -1.0e7 + 0.125, 0.3333333333333333};
    engine::Json j = engine::Json::object().set("pos", engine::Json::array().push(pos.x).push(pos.y).push(pos.z));
    std::string text = j.dump();
    engine::Json back = engine::Json::parse(text);
    CHECK_EQ(back["pos"].at(0).num(0.0), pos.x);                    // exactly, not "close": the shortest text that reads back identical
    CHECK_EQ(back["pos"].at(1).num(0.0), pos.y);
    CHECK_EQ(back["pos"].at(2).num(0.0), pos.z);
    CHECK((double)(float)pos.x != pos.x);                           // the old float path could not have held this number
}

// ---- 6. the whole flight, step by step, the way the real modules do it ----
// The rigs below are miniatures of the actual fixed-step loop, built from the SAME pure rules the modules call (gravity::acceleration/kick,
// orbit::settleStep/offsetAt, world::padPose): ship/gravity kicks the velocity, ship_core drifts the position, ship/orbit_lock and ship/docking
// place it. Each rig runs twice from the same start with the same float velocity - once with the double position the ship has now, once with
// the float position it had before - so the ONLY difference is the type of the ship's own position. That is the before/after.

namespace {
namespace prec {

// One planet, the way gravity::buildSources would describe today's Planet 1 (radius 408, orbit.gravity_scale 60), placed wherever a test wants it.
inline gravity::Source planetAt(const Vec3d& where, double radius = 408.0) {
    gravity::Source s;
    s.pos = where; s.radius = radius; s.mu = orbit::bodyMu(radius, 60.0); s.soi = 1e30; s.kind = 1;
    return s;
}

// ship/gravity + ship_core, one fixed step. `posF` is the old float position; `posD` the new double one. Velocity is float in BOTH (IShip's
// velocity is, and stays, float: m/s values lose nothing) so the position type is the only variable.
struct Orbiter {
    gravity::Source body;
    Vec3d posD; Vec3 velD;
    Vec3 posF; Vec3 velF;
    double minRd = 1e300, maxRd = 0, minRf = 1e300, maxRf = 0;

    void stepD(float dt) {
        Vec3d a = gravity::acceleration(body, posD, 0.0, 200.0);
        Vec3d v = gravity::kick({velD.x, velD.y, velD.z}, a, dt);
        velD = {(float)v.x, (float)v.y, (float)v.z};                  // IShip::setVelocity(engine::Vec3)
        ship::rules::integrate(posD, velD, dt);
        double r = orbit::length(orbit::sub(posD, body.pos));
        minRd = std::min(minRd, r); maxRd = std::max(maxRd, r);
    }
    void stepF(float dt) {
        Vec3d a = gravity::acceleration(body, {posF.x, posF.y, posF.z}, 0.0, 200.0);
        Vec3d v = gravity::kick({velF.x, velF.y, velF.z}, a, dt);
        velF = {(float)v.x, (float)v.y, (float)v.z};
        posF += velF * dt;                                            // the OLD ship_core integration
        double r = orbit::length(orbit::sub({posF.x, posF.y, posF.z}, body.pos));
        minRf = std::min(minRf, r); maxRf = std::max(maxRf, r);
    }
    void run(int steps, float dt) { for (int i = 0; i < steps; i++) { stepD(dt); stepF(dt); } }
    double eccD() const { return maxRd > 0 ? (maxRd - minRd) / (maxRd + minRd) : 0; }
    double eccF() const { return maxRf > 0 ? (maxRf - minRf) / (maxRf + minRf) : 0; }
};

// A circular orbit started at `radiusFactor` body radii, at the analytic circular speed: exactly what --gravity-scenario=orbit sets up.
inline Orbiter circularOrbiter(const Vec3d& bodyAt, double radiusFactor = 2.5) {
    Orbiter o;
    o.body = planetAt(bodyAt);
    double r = o.body.radius * radiusFactor;
    o.posD = orbit::add(o.body.pos, {r, 0, 0});
    o.posF = {(float)o.posD.x, (float)o.posD.y, (float)o.posD.z};
    float sp = (float)orbit::circularSpeed(o.body.mu, r);
    o.velD = o.velF = {0, 0, sp};
    return o;
}

// ship/orbit_lock, one fixed step: the analytic target for the next step, then velocity = (target - position) / dt.
struct Locker {
    Vec3d body;
    orbit::Orbit orb;
    Vec3d relVel0, off;
    double elapsed = 0, settle = 1.5;
    Vec3d posD; Vec3 posF;
    double worstRadiusErrD = 0, worstRadiusErrF = 0, worstSpeedD = 0, worstSpeedF = 0;

    void run(int steps, float dt) {
        for (int i = 0; i < steps; i++) {
            elapsed += dt;
            Vec3d offset;
            if (elapsed - dt < settle) off = offset = orbit::settleStep(orb, relVel0, off, elapsed - dt, dt, settle);
            else offset = orbit::offsetAt(orb, elapsed);
            Vec3d target = orbit::add(body, offset);
            // after: the lock reads the ship's DOUBLE position
            Vec3d vD = orbit::mul(orbit::sub(target, posD), 1.0 / dt);
            Vec3 vDf{(float)vD.x, (float)vD.y, (float)vD.z};
            ship::rules::integrate(posD, vDf, dt);
            // before: the same arithmetic, but the ship's position arrives (and is stored) as a float
            Vec3d vF = orbit::mul(orbit::sub(target, {posF.x, posF.y, posF.z}), 1.0 / dt);
            Vec3 vFf{(float)vF.x, (float)vF.y, (float)vF.z};
            posF += vFf * dt;
            if (elapsed > settle) {                                    // only judge the lock once it has settled
                worstRadiusErrD = std::max(worstRadiusErrD, std::abs(orbit::length(orbit::sub(posD, body)) - orb.radius));
                worstRadiusErrF = std::max(worstRadiusErrF, std::abs(orbit::length(orbit::sub({posF.x, posF.y, posF.z}, body)) - orb.radius));
                worstSpeedD = std::max(worstSpeedD, std::abs((double)engine::length(vDf) - orb.speed));
                worstSpeedF = std::max(worstSpeedF, std::abs((double)engine::length(vFf) - orb.speed));
            }
        }
    }
};

inline Locker lockerAt(const Vec3d& bodyAt, double radius = 408.0, double radiusFactor = 2.5) {
    Locker l;
    l.body = bodyAt;
    double r = radius * radiusFactor;
    Vec3d rel{r, 0, 0};
    l.orb = orbit::makeOrbitWithNormal(rel, {0, 1, 0}, orbit::bodyMu(radius, 60.0));
    l.relVel0 = orbit::circularVelocityAt(l.orb, rel);
    l.off = rel;
    l.posD = orbit::add(bodyAt, rel);
    l.posF = {(float)l.posD.x, (float)l.posD.y, (float)l.posD.z};
    return l;
}

// ship/docking's hold: every step the pad point is recomputed from the station's (moving, spinning) pose and the ship is PLACED there.
// "Held within tolerance" means the ship sits on the pad point, so the miss distance is what matters.
struct Holder {
    world::StationPose st;
    world::PadHeading heading{0, 1};
    double restHeight = 1.5;
    double worstD = 0, worstF = 0;     // worst distance from the pad point the ship was actually placed at
    double worstStepD = 0, worstStepF = 0;   // worst frame-to-frame jump of the ship on a station that is barely moving = visible judder

    void run(int steps, float dt) {
        Vec3d prevD{}, prevF{};
        bool have = false;
        for (int i = 0; i < steps; i++) {
            st.pos = orbit::add(st.pos, orbit::mul(st.vel, dt));                      // the station drifts along its orbit
            st.forward = world::spunForward(st.up, st.spinRate * dt * (double)(i + 1));
            world::PadPose pad = world::padPose(st, heading, restHeight);
            Vec3d shipD = pad.pos;                                                    // after: setPoseD places the ship exactly on the pad point
            Vec3 shipF{(float)pad.pos.x, (float)pad.pos.y, (float)pad.pos.z};         // before: setPose narrowed it to a float
            worstD = std::max(worstD, orbit::length(orbit::sub(shipD, pad.pos)));
            worstF = std::max(worstF, orbit::length(orbit::sub({shipF.x, shipF.y, shipF.z}, pad.pos)));
            if (have) {
                double moveTruth = orbit::length(orbit::sub(pad.pos, prevD));         // how far the pad point really moved this step
                worstStepD = std::max(worstStepD, std::abs(orbit::length(orbit::sub(shipD, prevD)) - moveTruth));
                worstStepF = std::max(worstStepF, std::abs(orbit::length(orbit::sub({shipF.x, shipF.y, shipF.z}, prevF)) - moveTruth));
            }
            prevD = shipD; prevF = {shipF.x, shipF.y, shipF.z};
            have = true;
        }
    }
};

inline Holder holderAt(const Vec3d& where) {
    Holder h;
    h.st.pos = where;
    h.st.vel = {0, 0, 12.0};            // a station on its own orbit
    h.st.up = {0, 1, 0};
    h.st.forward = {0, 0, 1};
    h.st.spinRate = 0.05;
    h.st.padTop = 23.2;
    return h;
}

// The render chain as it stands TODAY (still float past ship_core - the follow-up task's job): the eye is stored in a float view matrix and
// every camera-relative world pass recovers it back out in double (star_system.cpp:193, asteroids.cpp:187, stations.cpp:203).
inline Vec3d recoverEye(const float m[16]) {
    return {-((double)m[0] * m[12] + (double)m[1] * m[13] + (double)m[2] * m[14]),
            -((double)m[4] * m[12] + (double)m[5] * m[13] + (double)m[6] * m[14]),
            -((double)m[8] * m[12] + (double)m[9] * m[13] + (double)m[10] * m[14])};
}

struct Jitter { double worstError = 0, worstFrameStep = 0, meanFrameStep = 0; };

// Fly `steps` frames from `start` at `vel` and measure, per frame, how far the recovered render eye is from the ship's true double eye and -
// what the pilot actually sees - how much that error MOVES between frames.
inline Jitter measureCameraJitter(const Vec3d& start, const Vec3& vel, int steps, float dt) {
    Vec3d p = start;
    Jitter j;
    Vec3d prevErr{};
    double sum = 0; int n = 0;
    for (int i = 0; i < steps; i++) {
        ship::rules::integrate(p, vel, dt);
        core::Pose ship;
        ship.posD = p;
        ship.pos = ship::rules::toF(p);
        ship.fwd = {0, 0, -1}; ship.up = {0, 1, 0};
        core::Pose view = core::cam::chasePose(ship, 27.0f, 7.0f);
        float m[16];
        core::cam::viewMatrix(view, m);
        Vec3d eye = recoverEye(m);
        Vec3d err{eye.x - view.posD.x, eye.y - view.posD.y, eye.z - view.posD.z};
        j.worstError = std::max(j.worstError, len(err));
        if (i > 0) {
            double step = len(sub(err, prevErr));
            j.worstFrameStep = std::max(j.worstFrameStep, step);
            sum += step; n++;
        }
        prevErr = err;
    }
    j.meanFrameStep = n > 0 ? sum / n : 0;
    return j;
}

// Real coordinates, not round ones: 1e7 and 5e9 are both EXACTLY representable as floats, so a rig placed at {1e7, 0, 0} measures
// far less damage than the game would really take. These are the shapes the seeded system actually produces (seed 1234 puts the sun at
// -4649013052, -10563356, 1840262498 once world.sun_distance is the realistic 5e9).
inline const Vec3d kToday{34012.75, -7120.5, 21344.25};                          // |.| = 4.08e4: today's Planet 1 distance from the spawn point
inline const Vec3d kTenMillion{9.0e6 + 137.25, -2.5e6 + 42.5, 3.6e6 - 88.75};    // |.| = 1.00e7: the task's minimum proof distance
inline const Vec3d kFiveBillion{-4649013052.0, -10563356.0, 1840262498.0};       // |.| = 5.00e9: Neptune-orbit scale, where the rescale is heading

} // namespace prec
} // namespace

// ---- 6a. today's scale: the new double path and the old float path fly the SAME flight ----
// This is the regression bar: nothing about WHAT the game does may change, only how far from the origin it can do it.

TEST(precision_regression_gravity_orbit_at_todays_scale_is_unchanged) {
    prec::Orbiter o = prec::circularOrbiter(prec::kToday);
    o.run(18000, 1.0f / 60.0f);                                       // 300 s, about 4.6 orbits at 98.95 m/s (a 1.649 m step per frame)
    // Measured. double: radius 1019.1677 - 1020.8332, eccentricity 8.164e-04.  float: 1019.0416 - 1020.7852, eccentricity 8.547e-04.
    // The same orbit, to 13 cm of radius: the tiny difference is the old float path's own accumulated rounding, which is what this task
    // removes. After five minutes the two ships are 3.59 m apart ALONG the orbit (a phase lag, not a different orbit): 0.06% of the
    // 30,000 m they have flown, and nothing in the game reads absolute position to that accuracy.
    CHECK(o.eccD() < 1e-3);
    CHECK(o.eccF() < 1e-3);
    CHECK(std::abs(o.eccD() - o.eccF()) < 1e-4);                      // the same orbit, not merely two stable ones
    CHECK(std::abs(o.minRd - o.minRf) < 0.5);                         // measured 0.126 m
    CHECK(std::abs(o.maxRd - o.maxRf) < 0.5);                         // measured 0.048 m
    CHECK(prec::dist(o.posD, {o.posF.x, o.posF.y, o.posF.z}) < 10.0); // measured 3.59 m of phase lag after 300 s
}

TEST(precision_regression_orbit_lock_at_todays_scale_is_unchanged) {
    prec::Locker l = prec::lockerAt(prec::kToday);
    l.run(9000, 1.0f / 60.0f);                                        // 150 s of a held lock
    // Measured: worst radius error 1.1999e-01 m (double) vs 1.2005e-01 m (float), worst speed error 1.0500 vs 1.1014 m/s against a
    // 98.95 m/s orbit. Identical to four digits - the lock re-derives its target from the analytic orbit every step, so at this scale
    // the float position never had room to accumulate. The two ships end 0.0009 m apart.
    CHECK(std::abs(l.worstRadiusErrD - l.worstRadiusErrF) < 0.01);
    CHECK(std::abs(l.worstSpeedD - l.worstSpeedF) < 0.2);
    CHECK(l.worstRadiusErrD < 0.2);
    CHECK(l.worstSpeedD < 1.2);
    CHECK(prec::dist(l.posD, {l.posF.x, l.posF.y, l.posF.z}) < 0.01);
}

TEST(precision_regression_dock_hold_at_todays_scale_is_unchanged) {
    prec::Holder h = prec::holderAt(prec::kToday);
    h.run(3600, 1.0f / 60.0f);                                        // a minute parked on a drifting, spinning pad
    // Measured: the ship sits exactly on the pad point in double, and 8.4e-04 m (0.8 mm) off it in float, with 1.2 mm of frame-to-frame
    // judder. Both are far inside docking's own hold tolerance, so a dock at today's distances behaves exactly as it did.
    CHECK(h.worstD == 0.0);
    CHECK(h.worstF < 0.01);
    CHECK(h.worstStepF < 0.01);
}

// ---- 6b. far from the origin: the same three, and only the double path survives ----

TEST(precision_gravity_orbit_stays_stable_at_ten_million_units) {
    prec::Orbiter o = prec::circularOrbiter(prec::kTenMillion);
    o.run(18000, 1.0f / 60.0f);                                       // the same 300 s / 4.6 orbits as the regression above
    // Measured. double: radius 1019.1677 - 1020.8332, eccentricity 8.164e-04 - digit for digit the orbit it flies at the origin; the
    // distance from the origin changes nothing at all.
    // float: radius 1015.7503 - 1187.2646, eccentricity 7.785e-02. 95 x more eccentric, a 171 m radius swing on a 1020 m orbit, and the
    // ship ends 1,749 m from where it belongs - because one float ULP out here is 1.0 m and the ship's step is 1.649 m, so every single
    // frame of the orbit is rounded to the nearest metre.
    CHECK(o.eccD() < 1e-3);
    CHECK(o.maxRd - o.minRd < 2.0);
    CHECK(o.eccF() > 20.0 * o.eccD());
    CHECK(o.maxRf - o.minRf > 50.0);
    CHECK(prec::dist(o.posD, {o.posF.x, o.posF.y, o.posF.z}) > 500.0);
}

TEST(precision_gravity_orbit_stays_stable_at_five_billion_units) {
    prec::Orbiter o = prec::circularOrbiter(prec::kFiveBillion);
    o.run(18000, 1.0f / 60.0f);
    // Measured. double: radius 1019.1675 - 1020.8348, eccentricity 8.173e-04 - the same orbit again, 5 billion units out.
    // float: one ULP here is 512 m, so the 1.649 m step rounds to nothing and the ship NEVER MOVES: its radius is pinned at 828.5433 m
    // for all 18,000 steps (spread exactly 0). It is not even on the right orbit - rounding the start position alone put it 191 m off -
    // and no amount of thrust or gravity can ever move it again. This is the failure this task exists to fix.
    CHECK(o.eccD() < 1e-3);
    CHECK(o.maxRd - o.minRd < 2.0);
    CHECK(o.maxRf - o.minRf == 0.0);                                  // frozen: five minutes of orbital motion moved it zero metres
    CHECK(std::abs(o.minRf - 1020.0) > 100.0);                        // and it started 191 m off its own orbit radius
}

TEST(precision_orbit_lock_settles_far_from_the_origin) {
    prec::Locker l = prec::lockerAt(prec::kTenMillion);
    l.run(9000, 1.0f / 60.0f);
    // Measured at 1e7. double: worst radius error 1.1999e-01 m, worst speed error 1.0500 m/s - identical to today's scale, the lock
    // settles and holds exactly as it always did.
    // float: worst radius error 5.3861e-01 m and the commanded velocity misses by 30.63 m/s on a 98.95 m/s orbit. The lock steers with
    // (target - position) / dt and dt is 1/60, so every metre of position rounding becomes 60 m/s of commanded velocity: a 31% speed error.
    CHECK(l.worstRadiusErrD < 0.2);
    CHECK(l.worstSpeedD < 1.2);
    CHECK(l.worstRadiusErrF > 0.3);
    CHECK(l.worstSpeedF > 10.0);
}

TEST(precision_orbit_lock_settles_at_five_billion_units) {
    prec::Locker l = prec::lockerAt(prec::kFiveBillion);
    l.run(3600, 1.0f / 60.0f);                                        // 60 s
    // Measured at 5e9. double: worst radius error 1.1999e-01 m, worst speed error 1.0500 m/s - still exactly today's numbers.
    // float: worst radius error 256.42 m and 15,579 m/s of commanded velocity - 157 x the orbital speed, pure rounding noise. The lock
    // would fling the ship off the moment it engaged.
    CHECK(l.worstRadiusErrD < 0.2);
    CHECK(l.worstSpeedD < 1.2);
    CHECK(l.worstRadiusErrF > 100.0);
    CHECK(l.worstSpeedF > 1000.0);
}

TEST(precision_dock_hold_stays_within_tolerance_far_from_the_origin) {
    prec::Holder a = prec::holderAt(prec::kTenMillion);
    a.run(3600, 1.0f / 60.0f);
    // Measured at 1e7: the double hold is exact (0 m off the pad point, every frame); the float one sits up to 0.274 m off it and jumps
    // 0.200 m between frames, on a pad the ship is supposed to be bolted to. docking's hold tolerance is centimetres.
    CHECK(a.worstD == 0.0);
    CHECK(a.worstF > 0.1);
    CHECK(a.worstStepF > 0.1);

    prec::Holder b = prec::holderAt(prec::kFiveBillion);
    b.run(3600, 1.0f / 60.0f);
    // At 5e9 the double hold is still exact and the float ship is 206 m from its own landing pad, teleporting 128 m a frame.
    CHECK(b.worstD == 0.0);
    CHECK(b.worstF > 100.0);
    CHECK(b.worstStepF > 50.0);
}

// ---- 7. the camera chain (DIAGNOSTIC ONLY - explicitly OUT OF SCOPE here; this measures the follow-up task) ----

TEST(precision_camera_jitter_is_measured_but_not_yet_fixed) {
    const float dt = 1.0f / 60.0f;
    const engine::Vec3 vel{0, 0, -100.0f};
    prec::Jitter near = prec::measureCameraJitter({12.5, -3.25, 7.75}, vel, 600, dt);
    prec::Jitter far7 = prec::measureCameraJitter(prec::kTenMillion, vel, 600, dt);
    prec::Jitter far9 = prec::measureCameraJitter(prec::kFiveBillion, vel, 600, dt);

    // ship_core's position is exact now; this is purely the FLOAT render chain past it (core::Pose::pos -> cam::viewMatrix's
    // -dot(axis, pos), stored as a float -> the eye every camera-relative world pass recovers back out of that matrix). The number that
    // matters is not the error but how much it MOVES between frames, because that is the whole world appearing to shake:
    //   at the origin: error 3.1e-05 m, worst frame-to-frame 4.1e-05 m, mean 1.8e-05 m   - invisible; what the game ships today
    //   at 1e7:        error 2.6e-01 m, worst frame-to-frame 1.7e-01 m, mean 1.1e-01 m   - a ~17 cm world-wide shudder every frame
    //   at 1e8:        error 3.37    m, worst frame-to-frame 2.33    m, mean 1.94    m   - obvious, unplayable
    //   at 5e9:        error 216     m, worst frame-to-frame 126     m, mean 3.33    m   - the world is gone
    // Fixing it means carrying Pose::posD through viewMatrix and the camera-relative passes. That is the follow-up task, not this one.
    CHECK(near.worstFrameStep < 0.01);
    CHECK(far7.worstFrameStep > 0.05);
    CHECK(far7.worstFrameStep < 1.0);
    CHECK(far9.worstError > 100.0);
    CHECK(far9.worstFrameStep > 50.0);
    CHECK(far9.worstFrameStep > 100.0 * far7.worstFrameStep);         // it grows with the distance, exactly like a float ULP
}
