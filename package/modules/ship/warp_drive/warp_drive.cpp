// ship/warp_drive - toggle a fuel-burning speed boost along the ship's heading. Drives the ship only through ship::IShip.
// Rules in warp_rules.h, behaviour and tunables in docs/WARP.md.
#include <cstdlib>
#include "core/input_handler/input_api.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "ship/ship_core/ship_api.h"
#include "ship/warp_drive/warp_rules.h"

class WarpDrive : public engine::Module {
public:
    const char* name() const override { return "ship/warp_drive"; }
    std::vector<std::string> dependencies() const override { return {"core/input_handler"}; }
    // init after whichever ship provider exists (--fake-ship replaces ship_core's IShip)
    std::vector<std::string> optionalDependencies() const override { return {"ship/ship_core", "ship/fake_ship"}; }

    bool init(engine::Engine& eng) override {
        input_ = &eng.services.require<core::IInput>();
        if (!eng.services.get<ship::IShip>()) LOG_W("warp", "no ship::IShip service: the drive does nothing");
        auto& c = eng.config;
        p_.speed = c.get("warp.speed", 2000.0f, "warp speed cap, m/s");
        p_.accelFactor = c.get("warp.accel_factor", 2.0f, "warp acceleration = warp.speed * this, m/s^2");
        p_.fuelDrain = c.get("warp.fuel_drain", 3.33f, "warp fuel burned per second (about 30 s on a full tank)");
        p_.minFuel = c.get("warp.min_fuel", 1.0f, "fuel needed to engage the drive");
        p_.exitSpeed = c.get("warp.exit_speed", 200.0f, "speed cap when leaving warp, m/s (0 = keep the momentum)");
        // dev aids: behave as if toggle_warp was pressed on that engine frame
        autoOn_ = frameFlag(eng, "auto-warp");
        autoOff_ = frameFlag(eng, "auto-warp-off");
        return true;
    }

    void shutdown(engine::Engine& eng) override { if (engaged_) disengage(eng, "shutdown"); }

    void onUpdate(engine::Engine& eng, float) override {
        bool toggle = input_->pressed("toggle_warp");
        long f = (long)eng.frame();
        if (autoOn_ >= 0 && f == autoOn_) toggle = true;
        if (autoOff_ >= 0 && f == autoOff_) toggle = true;
        if (!toggle) return;
        if (eng.paused()) { LOG_D("warp", "toggle ignored: game is paused"); return; }
        auto* ship = eng.services.get<ship::IShip>();
        if (!ship) return;
        const auto& s = ship->status();
        if (warp::canEngage(engaged_, s.alive, s.warpFuel, p_)) {
            engaged_ = true;
            ship->setWarping(true);
            LOG_I("warp", "ENGAGED (fuel %.1f)", s.warpFuel);
        } else if (engaged_) {
            disengage(eng, "toggled off");
        } else {
            LOG_I("warp", "cannot engage (%s)", !s.alive ? "ship destroyed" : "not enough fuel");
        }
    }

    void onFixedUpdate(engine::Engine& eng, float dt) override {
        if (!engaged_) return;
        auto* ship = eng.services.get<ship::IShip>();
        if (!ship) { engaged_ = false; return; }   // the ship is gone: nothing left to switch off
        const auto& s = ship->status();
        auto v = ship->velocity(), f = ship->forward();
        auto r = warp::step(s.alive, s.warpFuel, {v.x, v.y, v.z}, {f.x, f.y, f.z}, dt, p_);
        if (!s.alive) { disengage(eng, "ship destroyed"); return; }
        bool stop = r.disengage;
        if (r.fuelToBurn > 0.0f && !ship->consumeWarpFuel(r.fuelToBurn)) stop = true;   // tank cannot cover it
        ship->setVelocity({r.velocity.x, r.velocity.y, r.velocity.z});
        if (stop) disengage(eng, "out of fuel");
    }

private:
    static long frameFlag(engine::Engine& eng, const char* key) {
        std::string v = eng.flagValue(key);
        return v.empty() ? -1 : std::atol(v.c_str());
    }

    void disengage(engine::Engine& eng, const char* why) {
        engaged_ = false;
        auto* ship = eng.services.get<ship::IShip>();   // may be gone during shutdown
        if (!ship) { LOG_I("warp", "DISENGAGED (%s), no ship", why); return; }
        ship->setWarping(false);
        auto v = ship->velocity();
        auto e = warp::exitVelocity({v.x, v.y, v.z}, p_);
        if (e.x != v.x || e.y != v.y || e.z != v.z) ship->setVelocity({e.x, e.y, e.z});
        LOG_I("warp", "DISENGAGED (%s), speed %.0f m/s", why, ship->status().speed);
    }

    core::IInput* input_ = nullptr;
    warp::Params p_;
    bool engaged_ = false;
    long autoOn_ = -1, autoOff_ = -1;
};

REGISTER_MODULE(WarpDrive);
