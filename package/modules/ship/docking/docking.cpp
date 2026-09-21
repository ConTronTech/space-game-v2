// ship/docking - the `dock` key (G): the ship eases onto the station's landing pad and then rides ON it, flat and facing a fixed direction in the
// station's frame, moving (and, for orbital stations, spinning) with it until thrust or the key releases it. The pose is analytic every fixed step:
// stationPose(t) x local offset (world::padPose), so nothing drifts; the ship's velocity is the pad point's true velocity. See docs/STATIONS.md.
#include <cmath>
#include <cstdlib>
#include "core/camera/camera_api.h"
#include "core/input_handler/input_api.h"
#include "core/save_system/save_api.h"
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
        approachSeconds_ = c.get("docking.approach_seconds", 1.5f, "seconds the ship takes to glide onto the landing pad after G (0 = instant)");
        restHeight_ = c.get("docking.rest_height", 1.4f, "height of the ship's origin above the pad surface when docked, units (the belly of the hull rests on the pad)");
        refill_ = c.get("docking.test_refill", false, "TEST ONLY: while docked, refill hull, shield and warp fuel (real fuel and shields come from mining later)");
        autoDock_ = frameFlag(eng, "auto-dock");
        autoUndock_ = frameFlag(eng, "auto-undock");
        eng.events.subscribe<ship::OrbitLockChanged>([this](const ship::OrbitLockChanged& e) { orbitLocked_ = e.locked; });
        eng.events.subscribe<ship::DamageTaken>([this, &eng](const ship::DamageTaken&) { if (state_ != State::Idle) undock(eng, "hit"); });
        eng.events.subscribe<ship::Died>([this, &eng](const ship::Died&) { if (state_ != State::Idle) undock(eng, "ship destroyed"); });
        eng.events.subscribe<ship::Respawned>([this, &eng](const ship::Respawned&) { if (state_ != State::Idle) undock(eng, "respawn"); });
        eng.events.subscribe<core::GameLoaded>([this, &eng](const core::GameLoaded&) { if (state_ != State::Idle) undock(eng, "game loaded"); });   // the saved ship position wins
        eng.services.provide<ship::IDocking>(this);
        eng_ = &eng;
        if (refill_) LOG_W("docking", "docking.test_refill is ON: docking refills hull, shield and warp fuel (test only)");
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        if (state_ != State::Idle) if (auto* ship = eng.services.get<ship::IShip>()) ship->setHeld(false);
        state_ = State::Idle;
        eng.services.withdraw<ship::IDocking>();
    }

    // ---- ship::IDocking ----
    bool busy() const override { return state_ != State::Idle; }        // approaching the pad OR docked
    bool docked() const override { return state_ == State::Docked; }   // true once the approach is finished (the Docked event fires then)
    std::string stationName() const override { return state_ == State::Docked ? name_ : std::string(); }
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
        if (autoOff) { if (state_ != State::Idle) undock(eng, "auto-undock"); return; }
        if (state_ != State::Idle) undock(eng, "key pressed"); else tryDock(eng);
    }

    void onFixedUpdate(engine::Engine& eng, float dt) override {
        if (state_ == State::Idle) return;
        auto* ship = eng.services.get<ship::IShip>();
        auto* st = eng.services.get<world::IStations>();
        if (!ship || !st || station_ >= st->count()) { if (ship) ship->setHeld(false); state_ = State::Idle; return; }
        const auto& s = ship->status();
        if (!s.alive) { undock(eng, "ship destroyed"); return; }
        if (s.warping) { undock(eng, "warp"); return; }
        if (world::shouldUndock(input_->value("thrust"), input_->value("strafe"), input_->value("lift"))) { undock(eng, "thrust"); return; }
        const world::StationPose pose = poseOf(st->info(station_));
        const world::PadPose target = world::padPose(pose, heading_, restHeight_);   // where the ship must be at THIS step's station pose
        auto p = ship->position();
        world::Vec3d cur{p.x, p.y, p.z};
        world::Vec3d pos = target.pos, fwd = target.forward, up = target.up, vel;
        if (state_ == State::Approaching) {
            elapsed_ += dt;
            double prog = world::approachProgress(elapsed_, approachSeconds_), k = world::smoothstep(prog);
            pos = world::sdetail::add(start_, world::sdetail::mul(world::sdetail::sub(target.pos, start_), k));
            world::blendOrientation(startFwd_, startUp_, target.forward, target.up, k, fwd, up);
            vel = world::steerVelocity(pos, cur, dt);                              // approach speed: consistent with the motion the camera sees
            if (prog >= 1.0) finishApproach(eng);
        } else {
            vel = world::padPointVelocity(pose, pos);                              // the pad point's true velocity: station motion + spin
        }
        ship->setVelocity({(float)vel.x, (float)vel.y, (float)vel.z});
        ship->setPose({(float)pos.x, (float)pos.y, (float)pos.z}, {(float)fwd.x, (float)fwd.y, (float)fwd.z}, {(float)up.x, (float)up.y, (float)up.z});
        if (state_ == State::Docked && refill_) { ship->heal(1000.0f); ship->addWarpFuel(1000.0f); ship->installShield(true); }
        if (state_ == State::Docked && (logTimer_ += dt) >= 1.0f) {
            logTimer_ = 0;
            auto q = ship->position();
            auto v = ship->velocity();
            world::Vec3d padPoint = world::padPose(pose, heading_, restHeight_).pos;
            double upDot = std::clamp(world::sdetail::dot(up, pose.up), -1.0, 1.0);
            LOG_D("docking", "docked at %s: distance to the pad point %.4f units, ship up vs station up %.5f deg, speed relative to the pad %.4f m/s",
                  name_.c_str(), world::sdetail::length(world::sdetail::sub({q.x, q.y, q.z}, padPoint)), std::acos(upDot) * 57.29578,
                  world::sdetail::length(world::sdetail::sub({v.x, v.y, v.z}, world::padPointVelocity(pose, {q.x, q.y, q.z}))));
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
        return world::canDock(ship.status().alive, ship.status().warping, orbitLocked_, state_ != State::Idle, true,
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
        const world::StationPose pose = poseOf(s);
        core::Pose view{};                                        // the ship's orientation: forward AND up come from the published pose
        view.fwd = ship->forward();
        if (auto* src = eng.services.get<core::ITransformSource>()) view = src->transform(1.0f);
        heading_ = world::captureHeading(pose, {view.fwd.x, view.fwd.y, view.fwd.z});   // heading projected onto the pad plane, kept in the station frame
        start_ = pos; startFwd_ = {view.fwd.x, view.fwd.y, view.fwd.z}; startUp_ = {view.up.x, view.up.y, view.up.z};
        elapsed_ = 0; logTimer_ = 0;
        state_ = State::Approaching;
        ship->setHeld(true);                                      // from now on the ship is placed by this module, not by the player
        LOG_I("docking", "approaching the pad of %s (distance %.1f units, %.1f s)", name_.c_str(), world::sdetail::length(world::sdetail::sub(pos, s.position)), (double)approachSeconds_);
        if (approachSeconds_ <= 0) finishApproach(eng);
    }

    // the Docked event fires at the END of the approach, so the HUD's DOCKED banner appears when the ship is on the pad
    void finishApproach(engine::Engine& eng) {
        if (state_ != State::Approaching) return;
        state_ = State::Docked;
        LOG_I("docking", "DOCKED at %s", name_.c_str());
        eng.events.emit(ship::Docked{name_});
    }

    void undock(engine::Engine& eng, const char* why) {
        if (state_ == State::Idle) return;
        const bool wasDocked = state_ == State::Docked;
        state_ = State::Idle;
        auto* ship = eng.services.get<ship::IShip>();
        auto* st = eng.services.get<world::IStations>();
        if (ship && st && station_ < st->count()) {
            auto in = st->info(station_);
            auto p = ship->position();
            world::Vec3d point{p.x, p.y, p.z};
            // leave with the velocity of the pad point under the ship (station motion + spin) and a small push out from the station
            world::Vec3d v = world::undockVelocity(world::padPointVelocity(poseOf(in), point), point, in.position, push_);
            ship->setVelocity({(float)v.x, (float)v.y, (float)v.z});
            ship->setHeld(false);
            LOG_I("docking", "leaving with %.2f m/s: pad point velocity %.2f m/s + push %.1f m/s (station velocity %.2f m/s)", world::sdetail::length(v),
                  world::sdetail::length(world::padPointVelocity(poseOf(in), point)), (double)push_, world::sdetail::length(in.velocity));
        } else if (ship) ship->setHeld(false);
        LOG_I("docking", "%s %s (%s)", wasDocked ? "UNDOCKED from" : "approach to", name_.c_str(), wasDocked ? why : (std::string("cancelled: ") + why).c_str());
        if (wasDocked) eng.events.emit(ship::Undocked{name_});
    }

    static world::StationPose poseOf(const world::StationInfo& in) {
        world::StationPose p;
        p.pos = in.position; p.vel = in.velocity; p.up = in.up; p.forward = in.forward; p.spinRate = in.spinRate; p.padTop = in.padTop;
        return p;
    }

    enum class State { Idle, Approaching, Docked };
    core::IInput* input_ = nullptr;
    engine::Engine* eng_ = nullptr;
    State state_ = State::Idle;
    bool orbitLocked_ = false, refill_ = false;
    int station_ = -1;
    std::string name_;
    world::PadHeading heading_;                       // the ship's heading on the pad, in the station frame
    world::Vec3d start_, startFwd_, startUp_;         // where the approach began (world)
    double elapsed_ = 0;
    float maxSpeed_ = 15, push_ = 3, logTimer_ = 0, approachSeconds_ = 1.5f, restHeight_ = 0.6f;
    long autoDock_ = -1, autoUndock_ = -1;
};

REGISTER_MODULE(Docking);
