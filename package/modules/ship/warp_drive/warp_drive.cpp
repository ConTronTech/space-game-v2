// ship/warp_drive - toggle a fuel-burning speed boost along the ship's heading. Drives the ship only through ship::IShip.
// Rules in warp_rules.h, behaviour and tunables in docs/WARP.md.
#include <cstdlib>
#include "core/input_handler/input_api.h"
#include "core/data_registry/data_api.h"
#include "core/save_system/save_api.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "ship/ship_core/ship_api.h"
#include "ship/warp_drive/warp_api.h"
#include "ship/warp_drive/warp_rules.h"

class WarpDrive : public engine::Module, public ship::IWarpDrive, public core::ISaveable {
public:
    const char* name() const override { return "ship/warp_drive"; }
    std::vector<std::string> dependencies() const override { return {"core/input_handler"}; }
    // init after whichever ship provider exists (--fake-ship replaces ship_core's IShip)
    std::vector<std::string> optionalDependencies() const override { return {"ship/ship_core", "ship/fake_ship", "core/data_registry", "core/save_system"}; }

    bool init(engine::Engine& eng) override {
        input_ = &eng.services.require<core::IInput>();
        if (!eng.services.get<ship::IShip>()) LOG_W("warp", "no ship::IShip service: the drive does nothing");
        auto& c = eng.config;
        base_.speed = c.get("warp.speed", 5000.0f, "base warp speed cap, m/s (times the upgrade level's speed multiplier)");
        base_.accelFactor = c.get("warp.accel_factor", 0.4f, "warp acceleration = warp.speed * this, m/s^2 (0.4 = 2.5 s from rest to full speed)");
        base_.fuelDrain = c.get("warp.fuel_drain", 3.33f, "warp fuel burned per second at level 0 (about 30 s on a full tank)");
        base_.minFuel = c.get("warp.min_fuel", 1.0f, "fuel needed to engage the drive");
        base_.exitSpeed = c.get("warp.exit_speed", 200.0f, "speed cap when leaving warp, m/s (0 = keep the momentum)");
        int startLevel = c.get("warp.upgrade_level", 0, "warp drive upgrade level 0..N (levels are in data/warp_drive.json); a saved game overrides it");
        // the upgrade table: data/warp_drive.json through IData if present, else the built-in defaults
        levels_ = warp::defaultLevels();
        if (auto* data = eng.services.get<core::IData>()) {
            auto ids = data->ids("warp_drive");
            if (!ids.empty()) {
                levels_.clear();
                for (auto& id : ids) {
                    const engine::Json& j = data->get("warp_drive", id);
                    levels_.push_back({(float)j["speed_mult"].num(1.0), (float)j["accel_mult"].num(1.0), (float)j["fuel_efficiency"].num(1.0)});
                }
            }
        }
        level_ = warp::clampLevel(startLevel, (int)levels_.size());
        refresh();
        eng.services.provide<ship::IWarpDrive>(this);
        if ((saves_ = eng.services.get<core::ISaveSystem>())) saves_->registerSaveable(this);
        LOG_I("warp", "drive level %d of %zu: %.0f m/s, %.1f s to full speed, %.2f fuel/s", level_, levels_.size(), p_.speed, warp::spoolSeconds(p_), p_.fuelDrain);
        // dev aids: behave as if toggle_warp was pressed on that engine frame
        autoOn_ = frameFlag(eng, "auto-warp");
        autoOff_ = frameFlag(eng, "auto-warp-off");
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        if (engaged_) disengage(eng, "shutdown");
        if (saves_) saves_->unregisterSaveable(this);
        eng.services.withdraw<ship::IWarpDrive>();
    }

    // ---- ship::IWarpDrive ----
    int level() const override { return level_; }
    int levelCount() const override { return (int)levels_.size(); }
    float maxSpeed() const override { return p_.speed; }
    bool engaged() const override { return engaged_; }
    void setLevel(int level) override {
        level_ = warp::clampLevel(level, (int)levels_.size());
        refresh();
        LOG_I("warp", "drive level %d: %.0f m/s, %.2f fuel/s", level_, p_.speed, p_.fuelDrain);
    }

    // ---- saving: just the upgrade level (a missing key keeps the current one) ----
    const char* saveId() const override { return "ship/warp_drive"; }
    engine::Json save() const override { return engine::Json::object().set("level", level_); }
    void load(const engine::Json& j) override {
        level_ = warp::clampLevel((int)j["level"].num(level_), (int)levels_.size());
        refresh();
    }

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
            logTimer_ = 0;
            ship->setWarping(true);
            { auto pp = ship->position(); LOG_I("warp", "ENGAGED (fuel %.1f) at (%.0f, %.0f, %.0f)", s.warpFuel, pp.x, pp.y, pp.z); }
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
        if ((logTimer_ += dt) >= 0.5f) { logTimer_ = 0; LOG_D("warp", "speed %.0f m/s, fuel %.1f", warp::length(r.velocity), ship->status().warpFuel); }
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
        { auto pp = ship->position(); LOG_I("warp", "DISENGAGED (%s), speed %.0f m/s at (%.0f, %.0f, %.0f)", why, ship->status().speed, pp.x, pp.y, pp.z); }
    }

    void refresh() { p_ = warp::effective(base_, levels_[level_]); }

    core::IInput* input_ = nullptr;
    core::ISaveSystem* saves_ = nullptr;
    warp::Params base_, p_;
    std::vector<warp::LevelDef> levels_;
    int level_ = 0;
    float logTimer_ = 0;
    bool engaged_ = false;
    long autoOn_ = -1, autoOff_ = -1;
};

REGISTER_MODULE(WarpDrive);
