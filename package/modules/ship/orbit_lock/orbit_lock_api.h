#pragma once
// Event of ship/orbit_lock, for the HUD:  eng.events.subscribe<ship::OrbitLockChanged>([](const ship::OrbitLockChanged& e) { ... });
#include <string>

namespace ship {

struct OrbitLockChanged {
    bool locked = false;
    std::string bodyName;      // "Planet 1", "Sun", ...; also set when the lock is released (the body it was locked to)
};

} // namespace ship
