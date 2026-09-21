// ship/docking - the `dock` key (G): hold the ship at a station in its dock zone, moving with it, until thrust or the key releases it.
// Steers through IShip::setVelocity exactly like ship/orbit_lock; nothing in ship_core changes. See docs/STATIONS.md.
#include <cstdlib>
#include "core/input_handler/input_api.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "ship/docking/docking_api.h"
#include "ship/orbit_lock/orbit_lock_api.h"
#include "ship/ship_core/ship_api.h"
#include "world/stations/stations_api.h"
#include "world/stations/stations_rules.h"

class Docking : public engine::Module, public ship::IDocking {
public:
    const char* name() const override { return "ship/docking"; }
    std::vector<std::string> dependencies() const override { return {"core/input_handler"}; }
    // run after the ship and the stations (the station positions must already be this step's)
    std::vector<std::string> optionalDependencies() const override { return {"ship/ship_core", "ship/fake_ship", "world/stations", "ship/orbit_lock"}; }

    bool init(engine::Engine& eng) override {
        input_ = &eng.services.require<core::IInput>();
        auto& c = eng.config;
        maxSpeed_ = c.get("docking.max_speed", 15.0f, "highest speed relative to the station at which docking works, m/s");
        push_ = c.get("docking.undock_push", 3.0f, "speed away from the station when undocking, m/s");
        refill_ = c.get("docking.test_refill", false, "TEST ONLY: while docked, refill hull, shield and warp fuel (real fuel and shields come from mining later)");
        autoDock_ = frameFlag(eng, "auto-dock");
        autoUndock_ = frameFlag(eng, "auto-undock");
        eng.events.subscribe<ship::OrbitLockChanged>([this](const ship::OrbitLockChanged& e) { orbitLocked_ = e.locked; });
        eng.events.subscribe<ship::DamageTaken>([this, &eng](const ship::DamageTaken&) { if (docked_) undock(eng, "hit"); });
        eng.events.subscribe<ship::Died>([this, &eng](const ship::Died&) { if (docked_) undock(eng, "ship destroyed"); });
        eng.events.subscribe<ship::Respawned>([this, &eng](const ship::Respawned&) { if (docked_) undock(eng, "respawn"); });
        eng.services.provide<ship::IDocking>(this);
        eng_ = &eng;
        if (refill_) LOG_W("docking", "docking.test_refill is ON: docking refills hull, shield and warp fuel (test only)");
        return true;
    }

    void shutdown(engine::Engine& eng) override { docked_ = false; eng.services.withdraw<ship::IDocking>(); }

    // ---- ship::IDocking ----
    bool docked() const override { return docked_; }
    std::string stationName() const override { return docked_ ? name_ : std::string(); }
    bool nearestDockable(std::string& name, float& distance, bool& ok, std::string& reason) const override {
        auto* st = eng_->services.get<world::IStations>();
        auto* ship = eng_->services.get<ship::IShip>();
        if (!st || !ship) return false;
        auto p = ship->position();
        world::Vec3d pos{p.x, p.y, p.z};
        int i = st->nearest(pos);
        if (i < 0) return false;
        auto s = st->info(i);
        auto check = evaluate(*ship, s, pos);
        name = s.name; distance = (float)world::sdetail::length(world::sdetail::sub(s.position, pos));
        ok = check == world::DockCheck::Ok;
        reason = world::dockReason(check);
        if (check == world::DockCheck::TooFast) reason += " (max " + std::to_string((int)maxSpeed_) + " m/s relative to the station)";
        return true;
    }

    void onUpdate(engine::Engine& eng, float) override {
        bool press = input_->pressed("dock");
        long f = (long)eng.frame();
        bool autoOn = autoDock_ >= 0 && f == autoDock_, autoOff = autoUndock_ >= 0 && f == autoUndock_;
        if (!press && !autoOn && !autoOff) return;
        if (eng.paused()) { LOG_D("docking", "key ignored: game is paused"); return; }
        if (autoOff) { if (docked_) undock(eng, "auto-undock"); return; }
        if (docked_) undock(eng, "key pressed"); else tryDock(eng);
    }

    void onFixedUpdate(engine::Engine& eng, float dt) override {
        if (!docked_) return;
        auto* ship = eng.services.get<ship::IShip>();
        auto* st = eng.services.get<world::IStations>();
        if (!ship || !st || station_ >= st->count()) { docked_ = false; return; }
        const auto& s = ship->status();
        if (!s.alive) { undock(eng, "ship destroyed"); return; }
        if (s.warping) { undock(eng, "warp"); return; }
        if (world::shouldUndock(input_->value("thrust"), input_->value("strafe"), input_->value("lift"))) { undock(eng, "thrust"); return; }
        auto in = st->info(station_);
        // hold the offset captured at docking: the ship must be at station + offset one step from now (the station's velocity extrapolated one step)
        world::Vec3d target = world::sdetail::add(world::sdetail::add(in.position, world::sdetail::mul(in.velocity, dt)), offset_);
        auto p = ship->position();
        world::Vec3d v = world::steerVelocity(target, {p.x, p.y, p.z}, dt);
        ship->setVelocity({(float)v.x, (float)v.y, (float)v.z});
        if (refill_) { ship->heal(1000.0f); ship->addWarpFuel(1000.0f); ship->installShield(true); }
        if ((logTimer_ += dt) >= 1.0f) {
            logTimer_ = 0;
            LOG_D("docking", "docked at %s: distance %.3f units, speed relative to it %.2f m/s", name_.c_str(),
                  world::sdetail::length(world::sdetail::sub({p.x, p.y, p.z}, in.position)), world::sdetail::length(world::sdetail::sub(v, in.velocity)));
        }
    }

private:
    static long frameFlag(engine::Engine& eng, const char* key) {
        std::string v = eng.flagValue(key);
        return v.empty() ? -1 : std::atol(v.c_str());
    }

    world::DockCheck evaluate(ship::IShip& ship, const world::StationInfo& s, const world::Vec3d& pos) const {
        auto v = ship.velocity();
        world::Vec3d rel = world::sdetail::sub({v.x, v.y, v.z}, s.velocity);
        return world::canDock(ship.status().alive, ship.status().warping, orbitLocked_, docked_, true,
                              world::sdetail::length(world::sdetail::sub(s.position, pos)), s.radius, world::sdetail::length(rel), maxSpeed_);
    }

    void tryDock(engine::Engine& eng) {
        auto* ship = eng.services.get<ship::IShip>();
        auto* st = eng.services.get<world::IStations>();
        if (!ship || !st) { LOG_I("docking", "cannot dock: no %s", ship ? "stations" : "ship"); return; }
        auto p = ship->position();
        world::Vec3d pos{p.x, p.y, p.z};
        int i = st->nearest(pos);
        if (i < 0) { LOG_I("docking", "cannot dock: %s", world::dockReason(world::DockCheck::NoStation)); return; }
        auto s = st->info(i);
        auto check = evaluate(*ship, s, pos);
        if (check != world::DockCheck::Ok) {
            auto v = ship->velocity();
            LOG_I("docking", "cannot dock at %s: %s (distance %.0f of %.0f, relative speed %.1f of %.0f m/s)", s.name.c_str(), world::dockReason(check),
                  world::sdetail::length(world::sdetail::sub(s.position, pos)), (double)s.radius,
                  world::sdetail::length(world::sdetail::sub({v.x, v.y, v.z}, s.velocity)), (double)maxSpeed_);
            return;
        }
        station_ = i; name_ = s.name;
        offset_ = world::sdetail::sub(pos, s.position);          // hold where we are: nothing jumps
        docked_ = true; logTimer_ = 0;
        LOG_I("docking", "DOCKED at %s (distance %.1f units)", name_.c_str(), world::sdetail::length(offset_));
        eng.events.emit(ship::Docked{name_});
    }

    void undock(engine::Engine& eng, const char* why) {
        docked_ = false;
        auto* ship = eng.services.get<ship::IShip>();
        auto* st = eng.services.get<world::IStations>();
        if (ship && st && station_ < st->count()) {
            auto in = st->info(station_);
            auto p = ship->position();
            world::Vec3d v = world::undockVelocity(in.velocity, {p.x, p.y, p.z}, in.position, push_);
            ship->setVelocity({(float)v.x, (float)v.y, (float)v.z});     // leave with the station's velocity and a small push out
        }
        LOG_I("docking", "UNDOCKED from %s (%s)", name_.c_str(), why);
        eng.events.emit(ship::Undocked{name_});
    }

    core::IInput* input_ = nullptr;
    engine::Engine* eng_ = nullptr;
    bool docked_ = false, orbitLocked_ = false, refill_ = false;
    int station_ = -1;
    std::string name_;
    world::Vec3d offset_;
    float maxSpeed_ = 15, push_ = 3, logTimer_ = 0;
    long autoDock_ = -1, autoUndock_ = -1;
};

REGISTER_MODULE(Docking);
