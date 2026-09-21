// ship/orbit_lock - toggle_orbit_lock (O): a stable circular orbit around the body whose surface is nearest.
// The ship is steered along an analytic orbit that follows the moving body, using only IShip::setVelocity. See docs/ORBIT_LOCK.md.
#include <cstdlib>
#include "core/input_handler/input_api.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "ship/orbit_lock/orbit_lock_api.h"
#include "ship/orbit_lock/orbit_lock_rules.h"
#include "ship/ship_core/ship_api.h"
#include "world/star_system/star_system_api.h"

class OrbitLock : public engine::Module {
public:
    const char* name() const override { return "ship/orbit_lock"; }
    std::vector<std::string> dependencies() const override { return {"core/input_handler"}; }
    // run after the ship (and the star system, so the body positions are already this step's)
    std::vector<std::string> optionalDependencies() const override { return {"ship/ship_core", "ship/fake_ship", "world/star_system"}; }

    bool init(engine::Engine& eng) override {
        input_ = &eng.services.require<core::IInput>();
        auto& c = eng.config;
        gravity_ = c.get("orbit.gravity_scale", 60.0f, "orbit gravity: mu = scale * radius^2, circular speed = sqrt(mu / distance from the body centre)");
        range_ = c.get("orbit.engage_range", 3.0f, "lock only within this many body radii above the surface");
        minAlt_ = c.get("orbit.min_altitude", 20.0f, "never lock closer than this above the surface, units");
        releaseOnThrust_ = c.get("orbit.release_on_thrust", true, "thrust, strafe, lift or brake releases the lock (you burned)");
        autoOn_ = frameFlag(eng, "auto-orbit-lock");
        autoOff_ = frameFlag(eng, "auto-orbit-lock-off");
        eng.events.subscribe<ship::DamageTaken>([this, &eng](const ship::DamageTaken&) { if (locked_) release(eng, "hit"); });
        eng.events.subscribe<ship::Died>([this, &eng](const ship::Died&) { if (locked_) release(eng, "ship destroyed"); });
        eng.events.subscribe<ship::Respawned>([this, &eng](const ship::Respawned&) { if (locked_) release(eng, "respawn"); });
        return true;
    }

    void shutdown(engine::Engine& eng) override { locked_ = false; (void)eng; }   // nothing to undo: the ship simply keeps its velocity

    void onUpdate(engine::Engine& eng, float) override {
        bool toggle = input_->pressed("toggle_orbit_lock");
        long f = (long)eng.frame();
        if (autoOn_ >= 0 && f == autoOn_) toggle = true;
        if (autoOff_ >= 0 && f == autoOff_) toggle = true;
        if (!toggle) return;
        if (eng.paused()) { LOG_D("orbit", "toggle ignored: game is paused"); return; }
        if (locked_) release(eng, "toggled off"); else engage(eng);
    }

    void onFixedUpdate(engine::Engine& eng, float dt) override {
        lastDt_ = dt > 0.0f ? dt : lastDt_;
        auto* sys = eng.services.get<world::IStarSystem>();
        // remember every body's position each step: the previous one gives the body's velocity when engaging
        if (sys) {
            const auto& bodies = sys->bodies();
            prevPos_.resize(bodies.size());
            if (!locked_) { for (size_t i = 0; i < bodies.size(); i++) prevPos_[i] = bodies[i].position; }
        }
        if (!locked_) return;
        auto* ship = eng.services.get<ship::IShip>();
        if (!ship || !sys || body_ >= (int)sys->bodies().size()) { release(eng, "ship or star system gone"); return; }
        const auto& st = ship->status();
        if (orbit::shouldRelease(st.alive, st.warping, input_->value("thrust"), input_->value("strafe"), input_->value("lift"),
                                 input_->down("brake"), releaseOnThrust_)) {
            release(eng, st.alive ? (st.warping ? "warp" : "thrust") : "ship destroyed");
            return;
        }
        // Where the ship must be at the next step: the body's next position (its current velocity extrapolated one step) plus the orbit offset.
        // The ship integrates position += velocity * dt, so the velocity that lands it there is (target - position) / dt. Re-derived from the
        // analytic orbit every step: errors never accumulate.
        world::Vec3d bp = sys->positionAt(body_);
        world::Vec3d bv = orbit::mul(orbit::sub(bp, prevPos_[body_]), 1.0 / dt);
        prevPos_[body_] = bp;
        elapsed_ += dt;
        world::Vec3d target = orbit::positionOnOrbit(orbit::add(bp, orbit::mul(bv, dt)), orbit_, elapsed_);
        auto p = ship->position();
        world::Vec3d v = orbit::mul(orbit::sub(target, {p.x, p.y, p.z}), 1.0 / dt);
        ship->setVelocity({(float)v.x, (float)v.y, (float)v.z});
        if ((logTimer_ += dt) >= 1.0f) {
            logTimer_ = 0;
            world::Vec3d rel = orbit::sub({p.x, p.y, p.z}, bp);
            world::Vec3d sv = {v.x - bv.x, v.y - bv.y, v.z - bv.z};
            LOG_D("orbit", "%s: radius %.3f (start %.3f), speed %.2f m/s relative to the body", name_.c_str(), orbit::length(rel), orbit_.radius, orbit::length(sv));
        }
    }

private:
    static long frameFlag(engine::Engine& eng, const char* key) {
        std::string v = eng.flagValue(key);
        return v.empty() ? -1 : std::atol(v.c_str());
    }

    void engage(engine::Engine& eng) {
        auto* ship = eng.services.get<ship::IShip>();
        auto* sys = eng.services.get<world::IStarSystem>();
        if (!ship || !sys) { LOG_I("orbit", "cannot lock: no %s", ship ? "star system" : "ship"); return; }
        const auto& st = ship->status();
        if (!st.alive) { LOG_I("orbit", "cannot lock: ship destroyed"); return; }
        if (st.warping) { LOG_I("orbit", "cannot lock while warping"); return; }
        const auto& bodies = sys->bodies();
        auto sp = ship->position();
        world::Vec3d pos{sp.x, sp.y, sp.z};
        std::vector<orbit::Candidate> cand;
        for (auto& b : bodies) cand.push_back({b.position, (double)b.radius});
        auto n = orbit::nearestBody(cand, pos);
        if (n.index < 0) { LOG_I("orbit", "cannot lock: no bodies"); return; }
        const auto& b = bodies[n.index];
        if (!orbit::inEngageRange(n.altitude, b.radius, range_, minAlt_)) {
            LOG_I("orbit", "cannot lock: %s is %.0f units above the surface (range %.0f to %.0f)", b.name.c_str(), n.altitude, (double)minAlt_, range_ * b.radius);
            return;
        }
        // body velocity from the last two steps; the ship's velocity relative to it decides which way round we orbit
        world::Vec3d bv{};
        if ((size_t)n.index < prevPos_.size()) bv = orbit::mul(orbit::sub(b.position, prevPos_[n.index]), 1.0 / lastDt_);
        auto v = ship->velocity(), f = ship->forward();
        world::Vec3d rel = orbit::sub(pos, b.position), relVel = orbit::sub({v.x, v.y, v.z}, bv);
        orbit_ = orbit::makeOrbit(rel, relVel, {f.x, f.y, f.z}, orbit::bodyMu(b.radius, gravity_));
        body_ = n.index; name_ = b.name; elapsed_ = 0; logTimer_ = 0;
        prevPos_.resize(bodies.size());
        prevPos_[body_] = b.position;
        locked_ = true;
        LOG_I("orbit", "LOCKED to %s: radius %.1f (altitude %.1f), circular speed %.1f m/s, period %.0f s", name_.c_str(), orbit_.radius, n.altitude, orbit_.speed,
              orbit_.omega > 0 ? 2.0 * 3.14159265358979 / orbit_.omega : 0.0);
        eng.events.emit(ship::OrbitLockChanged{true, name_});
    }

    void release(engine::Engine& eng, const char* why) {
        locked_ = false;
        LOG_I("orbit", "RELEASED from %s (%s)", name_.c_str(), why);
        eng.events.emit(ship::OrbitLockChanged{false, name_});
    }

    core::IInput* input_ = nullptr;
    bool locked_ = false, releaseOnThrust_ = true;
    float gravity_ = 60.0f, range_ = 3.0f, minAlt_ = 20.0f, logTimer_ = 0;
    long autoOn_ = -1, autoOff_ = -1;
    int body_ = -1;
    double elapsed_ = 0, lastDt_ = 1.0 / 60.0;
    std::string name_;
    orbit::Orbit orbit_;
    std::vector<world::Vec3d> prevPos_;
};

REGISTER_MODULE(OrbitLock);
