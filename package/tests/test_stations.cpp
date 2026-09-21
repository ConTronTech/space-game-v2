#include <cmath>
#include <set>
#include "tests/test.h"
#include "world/star_system/star_system_rules.h"
#include "world/stations/stations_rules.h"

namespace {
using world::Vec3d;
bool close(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol * std::max(1.0, std::fabs(b)); }
double len(const Vec3d& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
Vec3d sub(const Vec3d& a, const Vec3d& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

std::vector<world::ParentInfo> parents() {
    world::SystemParams p; p.seed = 1234;
    auto s = world::generateSystem(p);
    std::vector<world::ParentInfo> out;
    for (auto& b : s.bodies) {
        if (b.kind != world::BodyKind::Planet) continue;
        world::ParentInfo pi; pi.bodyId = b.id; pi.radius = b.radius; pi.name = b.name;
        for (auto& m : s.bodies) if (m.parent == b.id) pi.moonClearance = std::max(pi.moonClearance, m.orbitRadius + m.radius * 2.0);
        out.push_back(pi);
    }
    return out;
}
} // namespace

TEST(stations_generation_is_deterministic_and_within_the_rules) {
    auto ps = parents();
    auto a = world::generateStations(1234, 2, ps), b = world::generateStations(1234, 2, ps);
    CHECK_EQ((int)a.size(), 2); CHECK_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); i++) {
        CHECK_EQ(a[i].name, b[i].name); CHECK_EQ(a[i].parent, b[i].parent); CHECK(a[i].kind == b[i].kind);
        CHECK(close(a[i].orbitRadius, b[i].orbitRadius)); CHECK(close(a[i].lat, b[i].lat)); CHECK(close(a[i].phase, b[i].phase));
    }
    for (unsigned seed : {1u, 2u, 3u, 77u, 1234u, 99999u}) {
        for (int count : {0, 1, 2, 3, 9}) {
            auto s = world::generateStations(seed, count, ps);
            CHECK_EQ((int)s.size(), std::clamp(count, 0, 3));           // 1-3, clamped
            for (auto& st : s) {
                CHECK(st.scale == 2.0f || st.scale == 3.0f);
                CHECK((st.scale == 3.0f) == (st.parentRadius > 600.0));
                CHECK(close(st.dockRadius, st.scale * 60.0));
                CHECK(close(st.half, st.scale * 10.0));
                if (st.kind == world::StationKind::Orbital) {
                    CHECK(st.orbitRadius > world::clusterOuterRadius(st.parentRadius));            // outside the asteroid cluster shell
                    CHECK(std::fabs(st.tilt) <= 10.0 * 3.14159265358979 / 180.0 + 1e-9);          // +-10 degrees
                    CHECK(st.omega > 0);
                } else {
                    CHECK(std::fabs(st.lat) <= 60.0 * 3.14159265358979 / 180.0 + 1e-9);           // within +-60 degrees latitude
                    CHECK(st.surfaceRadius > st.parentRadius);                                     // above the surface
                }
            }
        }
    }
    CHECK(world::generateStations(1, 2, {}).empty());                                              // no planets: no stations
    // both kinds occur across seeds (50/50)
    int orb = 0, pla = 0;
    for (unsigned seed = 0; seed < 60; seed++) for (auto& st : world::generateStations(seed, 3, ps)) (st.kind == world::StationKind::Orbital ? orb : pla)++;
    CHECK(orb > 50 && pla > 50);
    auto other = world::generateStations(5, 3, ps);
    CHECK(other.size() == 3);
}

TEST(stations_orbital_outside_moons_and_cluster) {
    world::ParentInfo p; p.bodyId = 3; p.radius = 500; p.moonClearance = 4000; p.name = "P";
    double m = world::safeOrbitMin(500, 4000);
    CHECK(m > 4000 + 500 * 1.2);                                     // outside the moons
    CHECK(m > world::clusterOuterRadius(500));                       // outside the cluster shell (4.5 R + 500 + max(400, 3 R) = 4250)
    CHECK(close(world::clusterOuterRadius(500), 4.5 * 500 + 500 + 1500));
    CHECK(world::safeOrbitMin(500, 0) > world::clusterOuterRadius(500));
}

TEST(stations_orbital_position_is_analytic_periodic_and_on_the_circle) {
    world::Station s; s.kind = world::StationKind::Orbital; s.orbitRadius = 3000; s.omega = 0.004; s.phase = 1.0; s.tilt = 0.1; s.spinRate = 0.03;
    for (double t : {0.0, 10.0, 12345.6, 1e6, 1e8}) CHECK(close(len(world::stationOffset(s, t)), 3000.0, 1e-9));   // stays on the circle at any time
    double period = 2.0 * 3.14159265358979 / s.omega;
    CHECK(len(sub(world::stationOffset(s, 500.0), world::stationOffset(s, 500.0 + period))) < 1e-3);           // periodic
    CHECK(len(sub(world::stationOffset(s, 0.0), world::stationOffset(s, 100.0))) > 100.0);                     // it moves
    // same t, same answer regardless of the order asked (pausing / time_scale / loading only change t)
    Vec3d a = world::stationOffset(s, 777.0); world::stationOffset(s, 5.0); Vec3d b = world::stationOffset(s, 777.0);
    CHECK(len(sub(a, b)) < 1e-12);
    // the orbit plane is perpendicular to the up axis
    Vec3d up = world::stationUp(s);
    CHECK(close(len(up), 1.0, 1e-9));
    for (double t : {0.0, 300.0, 900.0}) { Vec3d o = world::stationOffset(s, t); CHECK(std::fabs(o.x * up.x + o.y * up.y + o.z * up.z) < 1e-6 * 3000.0); }
    CHECK(world::spinAngle(s, 1e6) >= 0.0 && world::spinAngle(s, 1e6) < 2.0 * 3.14159265358979);
}

TEST(stations_planetary_position_is_on_the_surface_sphere_with_normal_up) {
    world::Station s; s.kind = world::StationKind::Planetary; s.parentRadius = 400; s.half = 20; s.lat = 0.5; s.lon = 2.0; s.surfaceRadius = 400 * 1.02 + 20 * 3.4;
    Vec3d o = world::stationOffset(s, 0.0), o2 = world::stationOffset(s, 99999.0);
    CHECK(close(len(o), s.surfaceRadius, 1e-9));
    CHECK(len(sub(o, o2)) < 1e-9);                                    // planets do not rotate in V2: a planetary station is fixed relative to its planet
    CHECK(close(std::asin(o.y / len(o)), 0.5, 1e-9));                 // latitude
    Vec3d up = world::stationUp(s);
    CHECK(close(len(up), 1.0, 1e-9));
    CHECK(close(up.x, o.x / len(o), 1e-9)); CHECK(close(up.y, o.y / len(o), 1e-9)); CHECK(close(up.z, o.z / len(o), 1e-9));   // the normal points away from the planet
    CHECK(close(world::spinAngle(s, 5.0), 0.0));
}

TEST(stations_dock_rules_and_reasons) {
    using world::DockCheck;
    auto c = [](bool alive, bool warp, bool lock, bool docked, bool has, double d, double r, double v, double mv) { return world::canDock(alive, warp, lock, docked, has, d, r, v, mv); };
    CHECK(c(true, false, false, false, true, 100, 120, 5, 15) == DockCheck::Ok);
    CHECK(c(true, false, false, false, true, 120, 120, 15, 15) == DockCheck::Ok);              // exactly at the limits is fine
    CHECK(c(true, false, false, false, true, 120.5, 120, 5, 15) == DockCheck::TooFar);
    CHECK(c(true, false, false, false, true, 100, 120, 15.5, 15) == DockCheck::TooFast);
    CHECK(c(true, false, false, false, false, 0, 120, 0, 15) == DockCheck::NoStation);
    CHECK(c(false, false, false, false, true, 10, 120, 0, 15) == DockCheck::Dead);
    CHECK(c(true, true, false, false, true, 10, 120, 0, 15) == DockCheck::Warping);
    CHECK(c(true, false, true, false, true, 10, 120, 0, 15) == DockCheck::OrbitLocked);        // refused while orbit locked
    CHECK(c(true, false, false, true, true, 10, 120, 0, 15) == DockCheck::AlreadyDocked);
    CHECK_EQ(std::string(world::dockReason(DockCheck::TooFast)), std::string("too fast"));
    CHECK_EQ(std::string(world::dockReason(DockCheck::TooFar)), std::string("too far from the station"));
    for (auto k : {DockCheck::Ok, DockCheck::NoStation, DockCheck::Dead, DockCheck::Warping, DockCheck::OrbitLocked, DockCheck::AlreadyDocked, DockCheck::TooFar, DockCheck::TooFast})
        CHECK(std::string(world::dockReason(k)).size() > 1);
}

TEST(stations_dock_steering_follows_a_moving_station_and_undock_pushes_out) {
    // a station moving at (10, 0, 0) m/s, the ship 50 units above it: steering keeps the offset for many steps
    Vec3d station{1000, 2000, 3000}, vel{10, 0, 0}, offset{0, 50, 0}, ship{1000, 2050, 3000};
    double dt = 1.0 / 60;
    for (int i = 0; i < 6000; i++) {                                   // 100 s
        Vec3d target{station.x + vel.x * dt + offset.x, station.y + vel.y * dt + offset.y, station.z + vel.z * dt + offset.z};
        Vec3d v = world::steerVelocity(target, ship, dt);
        ship = {ship.x + v.x * dt, ship.y + v.y * dt, ship.z + v.z * dt};
        station = {station.x + vel.x * dt, station.y + vel.y * dt, station.z + vel.z * dt};
    }
    CHECK(len(sub(sub(ship, station), offset)) < 1e-6);                 // distance to the station stays constant
    CHECK(close(world::steerVelocity({1, 0, 0}, {0, 0, 0}, 0.5).x, 2.0));
    CHECK(len(world::steerVelocity({1, 0, 0}, {0, 0, 0}, 0.0)) == 0.0);  // dt 0: no division by zero
    Vec3d u = world::undockVelocity({10, 0, 0}, {0, 50, 0}, {0, 0, 0}, 3.0);
    CHECK(close(u.x, 10.0)); CHECK(close(u.y, 3.0)); CHECK(close(u.z, 0.0));   // station velocity + 3 m/s straight out
    CHECK(world::shouldUndock(0.5, 0, 0)); CHECK(world::shouldUndock(0, -0.5, 0)); CHECK(world::shouldUndock(0, 0, 0.5));
    CHECK(!world::shouldUndock(0.05, 0, 0)); CHECK(!world::shouldUndock(0, 0, 0));
}

// ---------------- landing pad: station frame, pad pose, approach ----------------
namespace {
double dotv(const Vec3d& a, const Vec3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3d addv(const Vec3d& a, const Vec3d& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3d mulv(const Vec3d& a, double k) { return {a.x * k, a.y * k, a.z * k}; }

// A station on a planet that itself moves in a straight line at 200 m/s; velocities are backward differences, exactly as world/stations computes them.
struct Sim {
    world::Station s;
    Vec3d parentAt(double t) const { return {1000.0 + 200.0 * t, 50.0 - 30.0 * t, -40000.0 + 120.0 * t}; }
    world::StationPose poseAt(double t, double dt) const {
        world::StationPose p;
        p.pos = addv(parentAt(t), world::stationOffset(s, t));
        Vec3d before = addv(parentAt(t - dt), world::stationOffset(s, t - dt));
        p.vel = mulv(sub(p.pos, before), 1.0 / dt);
        p.up = world::stationUp(s);
        p.forward = world::spunForward(p.up, world::spinAngle(s, t));
        p.spinRate = s.kind == world::StationKind::Orbital ? s.spinRate : 0.0;
        p.padTop = s.half * 1.16;
        return p;
    }
};
Sim orbitalSim() {
    Sim m;
    m.s.kind = world::StationKind::Orbital; m.s.half = 20; m.s.orbitRadius = 5000; m.s.omega = 0.005; m.s.phase = 0.7; m.s.tilt = 0.15; m.s.spinRate = 0.04;
    return m;
}
Sim planetarySim() {
    Sim m;
    m.s.kind = world::StationKind::Planetary; m.s.half = 30; m.s.lat = 0.4; m.s.lon = 2.0; m.s.surfaceRadius = 1500;
    return m;
}
} // namespace

TEST(station_frame_is_orthonormal_right_handed_and_spins_about_up) {
    for (Vec3d up : {Vec3d{0, 1, 0}, Vec3d{0, 0, 1}, Vec3d{1, 0, 0}, Vec3d{0.3, 0.5, 0.81}}) {
        double l = len(up); up = mulv(up, 1.0 / l);
        Vec3d f = world::referenceForward(up);
        CHECK(close(len(f), 1.0, 1e-9) && std::fabs(dotv(f, up)) < 1e-9);
        Vec3d r = world::sdetail::cross(up, f);
        Vec3d back = world::sdetail::cross(r, up);                                  // (right, up, forward) is right-handed: right x up = forward
        CHECK(len(sub(back, f)) < 1e-9);
        Vec3d quarter = world::spunForward(up, 3.14159265358979 / 2);               // a quarter turn about up moves forward onto +right
        CHECK(len(sub(quarter, r)) < 1e-9);
        CHECK(len(sub(world::spunForward(up, 0), f)) < 1e-12);
    }
}

TEST(pad_heading_is_captured_flat_and_kept_in_the_station_frame) {
    Sim m = orbitalSim();
    world::StationPose p0 = m.poseAt(10.0, 1.0 / 60);
    Vec3d shipFwd = {0.6, 0.2, -0.77};                                              // pitched: has a component along up
    world::PadHeading h = world::captureHeading(p0, shipFwd);
    CHECK(close(h.right * h.right + h.forward * h.forward, 1.0, 1e-9));             // unit
    world::PadPose a = world::padPose(p0, h, 0.6);
    CHECK(std::fabs(dotv(a.forward, a.up)) < 1e-9 && close(len(a.forward), 1.0, 1e-9));   // flat on the pad
    Vec3d flat = sub(shipFwd, mulv(p0.up, dotv(shipFwd, p0.up)));
    flat = mulv(flat, 1.0 / len(flat));
    CHECK(len(sub(a.forward, flat)) < 1e-9);                                        // == the heading projected onto the pad plane
    // a quarter of the spin period later the world heading has turned with the station: still the same angle to the station's forward
    world::StationPose p1 = m.poseAt(10.0 + (3.14159265358979 / 2) / m.s.spinRate, 1.0 / 60);
    world::PadPose b = world::padPose(p1, h, 0.6);
    CHECK(close(dotv(a.forward, p0.forward), dotv(b.forward, p1.forward), 1e-6));
    CHECK(len(sub(b.forward, world::sdetail::rotateAbout(a.forward, p1.up, 3.14159265358979 / 2))) < 1e-4);
    // straight up: falls back to the station forward
    world::PadHeading g = world::captureHeading(p0, p0.up);
    CHECK(close(g.forward, 1.0) && std::fabs(g.right) < 1e-12);
}

TEST(pad_pose_sits_on_the_pad_and_pad_point_velocity_includes_the_spin) {
    Sim m = orbitalSim();
    world::StationPose p = m.poseAt(5.0, 1.0 / 60);
    world::PadPose pad = world::padPose(p, {0, 1}, 0.6);
    CHECK(close(dotv(sub(pad.pos, p.pos), p.up), 20 * 1.16 + 0.6, 1e-9));           // pad top + rest height along up
    CHECK(len(sub(sub(pad.pos, p.pos), mulv(p.up, dotv(sub(pad.pos, p.pos), p.up)))) < 1e-9);   // and on the axis: the pad centre
    CHECK(len(sub(pad.up, p.up)) < 1e-12);                                          // belly to the pad
    Vec3d centreV = world::padPointVelocity(p, pad.pos);
    CHECK(len(sub(centreV, p.vel)) < 1e-9);                                         // on the spin axis: only the station's own velocity
    Vec3d point = addv(pad.pos, mulv(world::sdetail::cross(p.up, p.forward), 10.0));   // 10 units off-axis
    Vec3d v = world::padPointVelocity(p, point);
    CHECK(close(len(sub(v, p.vel)), m.s.spinRate * 10.0, 1e-9));                    // omega x r: 0.04 * 10 m/s
    CHECK(std::fabs(dotv(sub(v, p.vel), p.up)) < 1e-12);                            // tangential
    world::StationPose planetary = planetarySim().poseAt(5.0, 1.0 / 60);
    CHECK(len(sub(world::padPointVelocity(planetary, addv(planetary.pos, {5, 5, 5})), planetary.vel)) < 1e-12);   // no spin on a planetary station
}

TEST(pad_approach_easing_and_orientation_blend) {
    CHECK(close(world::smoothstep(0.0), 0.0) && close(world::smoothstep(1.0), 1.0) && close(world::smoothstep(0.5), 0.5));
    CHECK(world::smoothstep(0.1) < 0.1 && world::smoothstep(0.9) > 0.9);            // slow start, slow finish
    CHECK(close(world::smoothstep(-3), 0.0) && close(world::smoothstep(7), 1.0));   // clamped
    double prev = -1;
    for (double t = 0; t <= 1.0001; t += 0.05) { double v = world::smoothstep(t); CHECK(v >= prev - 1e-12); prev = v; }
    CHECK(close(world::approachProgress(0.75, 1.5), 0.5) && close(world::approachProgress(9, 1.5), 1.0) && close(world::approachProgress(-1, 1.5), 0.0));
    CHECK(close(world::approachProgress(0, 0), 1.0));                               // approach_seconds 0 = instant
    Vec3d f0{0, 0, -1}, u0{0, 1, 0}, f1{1, 0, 0}, u1{0, 0, 1};
    for (double s : {0.0, 0.1, 0.5, 0.9, 1.0}) {
        Vec3d f, u;
        world::blendOrientation(f0, u0, f1, u1, s, f, u);
        CHECK(close(len(f), 1.0, 1e-9) && close(len(u), 1.0, 1e-9) && std::fabs(dotv(f, u)) < 1e-9);   // stays orthonormal
    }
    Vec3d f, u;
    world::blendOrientation(f0, u0, f1, u1, 0.0, f, u);
    CHECK(len(sub(f, f0)) < 1e-9 && len(sub(u, u0)) < 1e-9);
    world::blendOrientation(f0, u0, f1, u1, 1.0, f, u);
    CHECK(len(sub(f, f1)) < 1e-9 && len(sub(u, u1)) < 1e-9);
    world::blendOrientation({0, 0, -1}, {0, 1, 0}, {0, 0, 1}, {0, 1, 0}, 0.5, f, u);   // exactly opposite headings: still a valid frame
    CHECK(close(len(f), 1.0, 1e-9) && std::fabs(dotv(f, u)) < 1e-9);
}

TEST(pad_follows_a_moving_spinning_station_without_drift) {
    for (int kind = 0; kind < 2; kind++) {
        Sim m = kind == 0 ? orbitalSim() : planetarySim();
        const double dt = 1.0 / 60;
        double t = 3.0;
        world::PadHeading h = world::captureHeading(m.poseAt(t, dt), {0.3, 0.1, -0.9});
        Vec3d prevShip = world::padPose(m.poseAt(t, dt), h, 0.6).pos;
        double worstDist = 0, worstUp = 0, worstRel = 0, worstFlat = 0;
        for (int step = 0; step < 3600; step++) {                                   // one simulated minute
            t += dt;
            world::StationPose p = m.poseAt(t, dt);
            world::PadPose pad = world::padPose(p, h, 0.6);
            // "distance to the pad point": the ship is placed analytically, so compare with an independent computation of the pad point
            Vec3d padPoint = addv(p.pos, mulv(p.up, p.padTop + 0.6));
            worstDist = std::max(worstDist, len(sub(pad.pos, padPoint)));
            worstUp = std::max(worstUp, len(sub(pad.up, p.up)));
            worstFlat = std::max(worstFlat, std::fabs(dotv(pad.forward, p.up)));
            // speed relative to the station: the ship's real motion between steps vs the pad point velocity
            Vec3d shipV = mulv(sub(pad.pos, prevShip), 1.0 / dt);
            worstRel = std::max(worstRel, len(sub(shipV, world::padPointVelocity(p, pad.pos))));
            prevShip = pad.pos;
        }
        CHECK(worstDist < 1e-6);                                                    // no drift: analytic
        CHECK(worstUp < 1e-4);                                                      // up stays the station's up
        CHECK(worstFlat < 1e-9);                                                    // heading stays in the pad plane
        CHECK(worstRel < 0.01);                                                     // relative speed ~0 (finite-difference noise only)
    }
}

TEST(undock_leaves_with_the_pad_point_velocity_plus_the_push) {
    Sim m = orbitalSim();
    world::StationPose p = m.poseAt(20.0, 1.0 / 60);
    world::PadPose pad = world::padPose(p, {1, 0}, 0.6);
    Vec3d off = addv(pad.pos, mulv(world::sdetail::cross(p.up, p.forward), 12.0));   // a ship parked 12 units off the spin axis
    Vec3d padV = world::padPointVelocity(p, off);
    Vec3d v = world::undockVelocity(padV, off, p.pos, 3.0);
    CHECK(close(len(sub(v, padV)), 3.0, 1e-9));                                     // exactly the push on top of the pad point velocity
    CHECK(dotv(sub(v, padV), sub(off, p.pos)) > 0);                                 // pointing away from the station
    CHECK(len(sub(v, p.vel)) > m.s.spinRate * 12.0 - 3.0 - 1e-6);                   // and the spin's contribution is in there, not just the centre velocity
}

TEST(stations_planetary_cube_grounded_on_terrain) {
    world::ParentInfo p; p.bodyId = 1; p.radius = 1100; p.name = "P";
    p.terrain.seed = world::mixSeed(1234, 1); p.terrain.terrainHeight = 0.03f;
    for (int k = 0; k < 40; k++) {
        double lat = -1.0 + k * 0.05, lon = k * 0.37; float half = 30;
        Vec3d up = world::latLonDir(lat, lon);
        double c = world::groundedRadius(p, lat, lon, half);
        double ground = world::localSurfaceRadius(p, up);
        CHECK(std::fabs(ground - 1100) <= 1100 * 0.03 + 1e-6);                          // local height within the terrain amplitude
        double bottom = c - half;
        CHECK(bottom <= ground - half * 0.2 + 1e-6);                                      // bottom face at least 10% of the height under the ground at the centre
        CHECK(bottom > ground - half * 2.0);                                              // but still resting on it (no pole: not sunk / floating by metres)
        world::Station s; s.kind = world::StationKind::Planetary; s.half = half; s.lat = lat; s.lon = lon; s.surfaceRadius = c;
        CHECK(close(len(world::stationOffset(s, 0)), c, 1e-9));                         // info().position = the cube centre at the grounded height
        double padAbove = c + half * 1.16 - ground;                                       // pad top (centre + 1.16 half) above the local ground
        CHECK(padAbove <= half * 1.96 + 1e-6 && padAbove > half * 0.2);
    }
    world::ParentInfo flat = p; flat.terrain.terrainHeight = 0;                           // smooth sphere: R + 0.8 half, less the curvature under the corners
    double c = world::groundedRadius(flat, 0.3, 1.0, 20);
    CHECK(c <= 1116 + 1e-9 && c > 1116 - 0.5);
}
