#pragma once
// Events and the guide service of ship/orbit_lock, for the HUD / radar:
//   eng.events.subscribe<ship::OrbitLockChanged>([](const ship::OrbitLockChanged& e) { ... });
//   auto* g = eng.services.get<ship::IOrbitGuide>();   if (g && g->active()) show(g->bodyName(), g->neededSpeed(), g->aligned() ? "press O" : g->refuseReason());
#include <string>

namespace ship {

struct OrbitLockChanged {
    bool locked = false;
    std::string bodyName;      // "Planet 1", "Sun", ...; also set when the lock is released (the body it was locked to)
};

// O was pressed but the ship is not lined up with the orbit (orbit.require_alignment): `reason` is the same text the guide shows.
struct OrbitLockRefused { std::string bodyName, reason; };

// The orbit the lock WOULD give from the ship's current state, and how far off the ship is. Valid while active(); the getters return 0 / "" otherwise.
class IOrbitGuide {
public:
    virtual ~IOrbitGuide() = default;
    virtual bool active() const = 0;               // a body is in guide range and the ship is free (not locked, docked, warping or dead)
    virtual std::string bodyName() const = 0;
    virtual float altitude() const = 0;            // above the body's surface, units
    virtual float neededSpeed() const = 0;         // circular speed here, m/s
    virtual float speedError() const = 0;          // speed relative to the body minus neededSpeed (positive = too fast)
    virtual float headingErrorDeg() const = 0;     // angle between the relative velocity and the orbit tangent
    virtual bool aligned() const = 0;              // O would lock now
    virtual std::string refuseReason() const = 0;  // why not ("" when aligned)
};

} // namespace ship
