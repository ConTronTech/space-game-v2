#pragma once
// Cargo pods: a small pool of drifting containers scattered through the system, always visible, each holding a little ore (and sometimes a
// crafted item). Fly close and the magnet pulls one in. world/cargo_pods provides it. docs/CARGO_PODS.md.
//
//     auto* cp = eng.services.get<world::ICargoPods>();          // null if the module is off (or cargo_pods.enabled = false)
//     for (int i = 0; cp && i < cp->count(); i++) if (cp->alive(i)) radar.marker(cp->position(i));
//
// Same query shape as world::IAsteroids: count() is the number of SLOTS (stable indices for the whole run), alive(i) says whether slot i
// holds a pod right now, activeCount() is how many do. No save: a reload re-rolls every slot from the seed.
#include <cstdint>
#include <string>
#include "world/star_system/star_system_api.h"   // world::Vec3d

namespace world {

class ICargoPods {
public:
    virtual ~ICargoPods() = default;
    virtual int count() const = 0;                 // pod slots (cargo_pods.pool_size)
    virtual int activeCount() const = 0;           // slots holding a pod right now
    virtual bool alive(int i) const = 0;           // slot i holds a pod right now
    virtual Vec3d position(int i) const = 0;       // its current world position; only valid while alive(i)
};

// ---- events (engine.events) ----
struct CargoPodSpawned { int slot = -1; double x = 0, y = 0, z = 0; };   // once, the step a pod appears (world position then)
// Once per pickup: oreGiven / amount = what the hold accepted this time ("" / 0 = none), item = a bonus crafted item accepted ("" = none).
// A pod whose ore or item did not fit stays (flashing red) and is picked up again once there is room, which emits another one.
struct CargoPodCollected { int slot = -1; std::string oreGiven; int amount = 0; std::string item; };

} // namespace world
