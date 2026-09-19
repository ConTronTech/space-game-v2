#pragma once
// THE SHIP CONTRACT. Other modules (HUD, warp drive, respawn, weapons, world hazards...) depend on this interface,
// never on the module that implements it. ship/ship_core provides it:   eng.services.provide<ship::IShip>(this);
//
//     auto* ship = eng.services.get<ship::IShip>();          // null if the ship module is off
//     float hp = ship->status().hp;
//     ship->applyDamage(25.0f, "asteroid");                  // shield first, then hull; emits DamageTaken / Died
//
// Change this file only when the contract genuinely needs to grow, additively where possible, and say so in your report:
// several modules are written against it in parallel.
#include <string>
#include "engine/math.h"

namespace ship {

struct ShipStatus {
    float hp = 100.0f, maxHp = 100.0f;                 // maxHp can grow (hull plating item)
    float shield = 0.0f, maxShield = 200.0f;           // disposable shield: no regen; gone when broken
    bool shieldInstalled = false, shieldEnabled = false;
    float warpFuel = 100.0f, maxWarpFuel = 100.0f;
    bool alive = true;
    bool warping = false;
    float speed = 0.0f;                                // m/s (length of velocity)
};

// ---- events (engine.events) ----
struct DamageTaken { float amount = 0; float absorbedByShield = 0; std::string source; };   // amount = what reached the hull
struct Died        { std::string cause; };
struct Respawned   {};
struct ShieldBroken {};
struct FuelEmpty   {};                                 // warp fuel just hit 0

class IShip {
public:
    virtual ~IShip() = default;

    virtual const ShipStatus& status() const = 0;
    virtual engine::Vec3 position() const = 0;
    virtual engine::Vec3 velocity() const = 0;
    virtual engine::Vec3 forward() const = 0;

    // ---- things other modules may do to the ship ----
    virtual void applyDamage(float amount, const std::string& source) = 0;   // shield absorbs first; Died when hp reaches 0
    virtual void heal(float hp) = 0;                                          // clamps to maxHp; ignored when dead
    virtual void addWarpFuel(float amount) = 0;                               // clamps to maxWarpFuel
    virtual bool consumeWarpFuel(float amount) = 0;                           // false (and nothing taken) if not enough
    virtual void addMaxHp(float amount) = 0;                                  // hull plating (permanent)
    virtual void installShield(bool enabled) = 0;                             // shield generator: fills the shield
    virtual void setVelocity(const engine::Vec3& v) = 0;                      // e.g. warp drive, docking
    virtual void kill(const std::string& cause) = 0;                          // instant death (sun, ...): emits Died
    virtual void respawn() = 0;                                               // full reset at the spawn point: emits Respawned
};

} // namespace ship
