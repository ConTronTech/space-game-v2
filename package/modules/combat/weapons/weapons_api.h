#pragma once
// combat/weapons: events and a read-only service for the HUD (crosshair heat bar, hit marker) and for later NPC / ship-damage code.
//
//     eng.events.subscribe<combat::ProjectileHit>([](const combat::ProjectileHit& e) { /* e.targetKind, e.id, e.damage, e.shooter */ });
//     auto* c = eng.services.get<combat::ICombat>();  c->heat(c->selected());
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
};

} // namespace combat
