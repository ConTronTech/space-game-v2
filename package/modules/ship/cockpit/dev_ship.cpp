// ship/cockpit_dev_ship - a stand-in ship::IShip for looking at the cockpit screens before ship/ship_core exists.
// INERT unless started with the flag  --cockpit-dev-ship[=hp:35,shield:120,fuel:10,speed:87,warp:1,dead:1]  (then it provides
// ship::IShip with those values; anything unnamed keeps a healthy default). It never provides IShip when another module already does.
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include "core/camera/camera_api.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "ship/ship_core/ship_api.h"

namespace cockpit {

class DevShip : public engine::Module, public ship::IShip {
public:
    const char* name() const override { return "ship/cockpit_dev_ship"; }
    std::vector<std::string> optionalDependencies() const override { return {"ship/ship_core", "gameplay/flight"}; }

    bool init(engine::Engine& eng) override {
        if (!eng.hasFlag("cockpit-dev-ship") && eng.flagValue("cockpit-dev-ship").empty()) return true;   // inert
        if (eng.services.get<ship::IShip>()) { LOG_I("cockpit", "dev ship not used: a real ship::IShip exists"); return true; }
        std::stringstream ss(eng.flagValue("cockpit-dev-ship"));
        std::string kv;
        while (std::getline(ss, kv, ',')) {
            size_t colon = kv.find(':');
            if (colon == std::string::npos) continue;
            std::string k = kv.substr(0, colon);
            float v = (float)std::atof(kv.c_str() + colon + 1);
            if (k == "hp") s_.hp = v;
            else if (k == "maxhp") s_.maxHp = v;
            else if (k == "shield") { s_.shield = v; s_.shieldInstalled = s_.shieldEnabled = true; }
            else if (k == "fuel") s_.warpFuel = v;
            else if (k == "speed") s_.speed = v;
            else if (k == "warp") s_.warping = v != 0;
            else if (k == "dead") s_.alive = v == 0;
            else LOG_W("cockpit", "--cockpit-dev-ship: unknown key '%s'", k.c_str());
        }
        eng_ = &eng;
        eng.services.provide<ship::IShip>(this);
        provided_ = true;
        LOG_I("cockpit", "dev ship provided: hp %.0f shield %.0f fuel %.0f speed %.0f", s_.hp, s_.shield, s_.warpFuel, s_.speed);
        return true;
    }
    void shutdown(engine::Engine& eng) override { if (provided_) eng.services.withdraw<ship::IShip>(); }

    const ship::ShipStatus& status() const override { return s_; }
    engine::Vec3 position() const override { return {}; }
    engine::Vec3 velocity() const override { return forward() * s_.speed; }
    engine::Vec3 forward() const override {   // follow the real pose when a module publishes one, so the heading is live
        if (eng_) if (auto* src = eng_->services.get<core::ITransformSource>()) return src->transform(1.0f).fwd;
        return {0, 0, -1};
    }

    void applyDamage(float amount, const std::string&) override { float a = std::min(amount, s_.shield); s_.shield -= a; s_.hp = std::max(0.0f, s_.hp - (amount - a)); }
    void heal(float hp) override { s_.hp = std::min(s_.maxHp, s_.hp + hp); }
    void addWarpFuel(float a) override { s_.warpFuel = std::min(s_.maxWarpFuel, s_.warpFuel + a); }
    bool consumeWarpFuel(float a) override { if (s_.warpFuel < a) return false; s_.warpFuel -= a; return true; }
    void addMaxHp(float a) override { s_.maxHp += a; }
    void installShield(bool enabled) override { s_.shieldInstalled = true; s_.shieldEnabled = enabled; s_.shield = s_.maxShield; }
    void setVelocity(const engine::Vec3& v) override { s_.speed = engine::length(v); }
    void kill(const std::string&) override { s_.hp = 0; s_.alive = false; }
    void respawn() override { s_ = ship::ShipStatus{}; }

private:
    engine::Engine* eng_ = nullptr;
    ship::ShipStatus s_;
    bool provided_ = false;
};

REGISTER_MODULE(DevShip);

} // namespace cockpit
