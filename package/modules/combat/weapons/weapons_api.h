#pragma once
// combat/weapons: events and a read-only service for the HUD (crosshair heat bar, hit marker) and for later NPC / ship-damage code.
//
//     eng.events.subscribe<combat::ProjectileHit>([](const combat::ProjectileHit& e) { /* e.targetKind, e.id, e.damage, e.shooter */ });
//     auto* c = eng.services.get<combat::ICombat>();  c->heat(c->selected());  c->lockState();  c->ammo(2);
//     auto* a = eng.services.get<combat::IAmmo>();    a->addMissiles(3);      // Missile Pack
#include <string>
#include "world/star_system/star_system_api.h"   // world::Vec3d

namespace combat {

// A bolt hit something. Asteroids take the damage (world::IAsteroids::damage); planets, moons, the sun and stations only get the effect.
struct ProjectileHit {
    std::string targetKind;          // "asteroid", "planet", "moon", "sun", "station"
    int id = -1;                     // asteroid index, star-system body id or station index
    world::Vec3d position;
    float damage = 0;
    int shooter = 0;                 // 0 = the player's ship; NPCs will get their own ids
};
struct WeaponChanged { int index = 0; std::string name; };
struct Overheated    { int index = 0; std::string name; };
// A missile exploded (impact, proximity fuse, or its lifetime ran out). Asteroids within `radius` took area damage (one ProjectileHit each).
struct MissileExploded { world::Vec3d position; float radius = 0; std::string reason; int shooter = 0; };

// Lock-on state (the T key): idle -> acquiring (target inside the cone for lock_time) -> locked -> lost (target died / out of range / left the cone).
enum class LockStateId { Idle = 0, Acquiring = 1, Locked = 2, Lost = 3 };

class ICombat {
public:
    virtual ~ICombat() = default;
    virtual int weaponCount() const = 0;
    virtual int selected() const = 0;                       // index of the selected weapon
    virtual std::string weaponName(int i) const = 0;
    virtual float heat(int i) const = 0;                    // 0..1
    virtual bool overheated(int i) const = 0;               // locked out until it recovers
    virtual bool firing() const = 0;                        // the trigger is held and the weapon is able to fire
    virtual float hitMarkerAge() const = 0;                 // seconds since one of our bolts / the beam last hit something (large = long ago)
    // ---- missiles and lock-on (4.2b; non-pure so older implementations keep working) ----
    virtual int ammo(int i) const { (void)i; return -1; }   // missiles left for a missile weapon; -1 = unlimited (blaster, beam)
    virtual bool noAmmo() const { return false; }           // the selected weapon is out of ammo: the HUD shows "NO MISSILES"
    virtual int missileCount() const { return 0; }          // missiles in flight
    virtual LockStateId lockState() const { return LockStateId::Idle; }
    virtual std::string lockTargetName() const { return {}; }   // e.g. "asteroid 412" (no ore: that needs the Ore Scanner); empty without a target
    virtual float lockTargetDistance() const { return 0; }  // units, ship to target centre
    virtual float lockTargetAngle() const { return 0; }     // degrees off the ship's nose
    virtual float lockProgress() const { return 0; }        // 0..1 while acquiring, 1 when locked
    // ---- the raw lock target for the Ore Scanner (5.5b): the HUD decides what to show (combat/weapons/scanner_rules.h lockInfoText) ----
    virtual int lockTargetId() const { return -1; }         // asteroid index, -1 without a target
    virtual std::string lockTargetOre() const { return {}; }    // its ore id ("iron"); only shown to the player with the "ore_scanner" perk
    virtual float lockTargetRadius() const { return 0; }    // units
};

// The missile rack. Crafting's Missile Pack goes through this (a full rack refuses and the pack is not consumed).
class IAmmo {
public:
    virtual ~IAmmo() = default;
    virtual int addMissiles(int n) = 0;                     // returns how many were accepted (0 = rack full)
    virtual int missiles() const = 0;
    virtual int maxMissiles() const = 0;
};

} // namespace combat
