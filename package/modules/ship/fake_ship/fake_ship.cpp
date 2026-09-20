// ship/fake_ship - a stand-in ship::IShip for developing/verifying ship consumers (HUD, ...) without ship_core.
// INERT unless launched with  --fake-ship=hp:35,shield:120,fuel:10[,dead][,hit:20][,speed:42]  - then it provides ship::IShip.
// Without the flag it never provides the service, so it coexists with the real ship/ship_core.
// Keys: hp, shield (installs the shield; 0 = broken), fuel, speed, hit (emits one DamageTaken), dead. Missing keys keep full defaults.
// The states' events (ShieldBroken for shield:0, FuelEmpty for fuel:0, Died for dead, DamageTaken for hit) fire once on the first update.
#include <cstdlib>
#include <sstream>
#include "engine/engine.h"
#include "engine/log.h"
#include "ship/ship_core/ship_api.h"

class FakeShip : public engine::Module, public ship::IShip {
public:
    const char* name() const override { return "ship/fake_ship"; }

    bool init(engine::Engine& eng) override {
        std::string spec = eng.flagValue("fake-ship");
        if (spec.empty()) return true;                       // inert: provide nothing
        std::stringstream ss(spec);
        std::string kv;
        while (std::getline(ss, kv, ',')) {
            auto colon = kv.find(':');
            std::string k = kv.substr(0, colon);
            float v = colon == std::string::npos ? 0.0f : (float)std::atof(kv.c_str() + colon + 1);
            if (k == "hp") st_.hp = v;
            else if (k == "shield") { st_.shieldInstalled = st_.shieldEnabled = true; st_.shield = v; shieldGiven_ = true; }
            else if (k == "fuel") { st_.warpFuel = v; fuelGiven_ = true; }
            else if (k == "speed") st_.speed = v;
            else if (k == "hit") hit_ = v;
            else if (k == "dead") st_.alive = false;
            else LOG_W("fake_ship", "unknown key '%s' in --fake-ship", k.c_str());
        }
        if (!st_.alive) st_.hp = 0;
        eng_ = &eng;
        active_ = true;
        eng.services.provide<ship::IShip>(this);
        LOG_I("fake_ship", "active: hp %.0f shield %.0f%s fuel %.0f%s", st_.hp, st_.shield, st_.shieldInstalled ? "" : " (none)", st_.warpFuel, st_.alive ? "" : " DEAD");
        return true;
    }
    void shutdown(engine::Engine& eng) override { if (active_) eng.services.withdraw<ship::IShip>(); }

    void onUpdate(engine::Engine& eng, float) override {
        if (!active_ || announced_) return;
        announced_ = true;
        if (hit_ > 0) eng.events.emit(ship::DamageTaken{hit_, 0, "fake"});
        if (shieldGiven_ && st_.shield <= 0) eng.events.emit(ship::ShieldBroken{});
        if (fuelGiven_ && st_.warpFuel <= 0) eng.events.emit(ship::FuelEmpty{});
        if (!st_.alive) eng.events.emit(ship::Died{"fake"});
    }

    // ---- ship::IShip: just enough to be a plausible target ----
    const ship::ShipStatus& status() const override { return st_; }
    engine::Vec3 position() const override { return {}; }
    engine::Vec3 velocity() const override { return {}; }
    engine::Vec3 forward() const override { return {0, 0, -1}; }
    void applyDamage(float amount, const std::string& source) override {
        float toShield = st_.shieldEnabled ? std::min(amount, st_.shield) : 0;
        st_.shield -= toShield; st_.hp = std::max(0.0f, st_.hp - (amount - toShield));
        eng_->events.emit(ship::DamageTaken{amount - toShield, toShield, source});
        if (st_.hp <= 0 && st_.alive) kill(source);
    }
    void heal(float hp) override { if (st_.alive) st_.hp = std::min(st_.maxHp, st_.hp + hp); }
    void addWarpFuel(float a) override { st_.warpFuel = std::min(st_.maxWarpFuel, st_.warpFuel + a); }
    bool consumeWarpFuel(float a) override { if (st_.warpFuel < a) return false; st_.warpFuel -= a; return true; }
    void addMaxHp(float a) override { st_.maxHp += a; }
    void installShield(bool e) override { st_.shieldInstalled = true; st_.shieldEnabled = e; st_.shield = st_.maxShield; }
    void setVelocity(const engine::Vec3&) override {}
    void kill(const std::string& cause) override { st_.alive = false; st_.hp = 0; eng_->events.emit(ship::Died{cause}); }
    void respawn() override { st_ = ship::ShipStatus{}; eng_->events.emit(ship::Respawned{}); }

private:
    ship::ShipStatus st_;
    engine::Engine* eng_ = nullptr;
    bool active_ = false, announced_ = false, shieldGiven_ = false, fuelGiven_ = false;
    float hit_ = 0;
};

REGISTER_MODULE(FakeShip);
