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
