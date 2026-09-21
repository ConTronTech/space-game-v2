// ship/respawn - after the ship dies, waits respawn.seconds and calls ship::IShip::respawn(). Provides ship::IRespawn for the HUD countdown.
// Polls status().alive every frame, so a save loaded while dead also counts down. Rules live in respawn_rules.h. See docs/RESPAWN.md.
#include "engine/engine.h"
#include "engine/log.h"
#include "ship/respawn/respawn_api.h"
#include "ship/respawn/respawn_rules.h"
#include "ship/ship_core/ship_api.h"

class Respawn : public engine::Module, public ship::IRespawn {
public:
    const char* name() const override { return "ship/respawn"; }
    std::vector<std::string> optionalDependencies() const override { return {"ship/ship_core", "ship/fake_ship"}; }

    bool init(engine::Engine& eng) override {
        float seconds = eng.config.get("respawn.seconds", 3.0f, "seconds between the ship being destroyed and respawning (minimum 0.5)");
        bool enabled = eng.config.get("respawn.enabled", true, "false = never respawn automatically (the destroyed screen stays)");
        rules_.configure(seconds, enabled);
        eng.services.provide<ship::IRespawn>(this);
        return true;
    }
    void shutdown(engine::Engine& eng) override {
        if (eng.services.get<ship::IRespawn>() == static_cast<ship::IRespawn*>(this)) eng.services.withdraw<ship::IRespawn>();
    }

    void onUpdate(engine::Engine& eng, float dt) override {
        auto* ship = eng.services.get<ship::IShip>();
        if (!ship) {
            if (!warned_) { LOG_W("respawn", "no ship::IShip service (ship module off?): nothing to respawn"); warned_ = true; }
            return;
        }
        if (rules_.update(ship->status().alive, dt, eng.paused())) {
            LOG_I("respawn", "respawning the ship");
            ship->respawn();
        }
    }

    bool counting() const override { return rules_.counting(); }
    float secondsLeft() const override { return rules_.secondsLeft(); }

private:
    ship::RespawnRules rules_;
    bool warned_ = false;
};

REGISTER_MODULE(Respawn);
