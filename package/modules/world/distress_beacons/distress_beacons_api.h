#pragma once
// Distress beacons: a recurring, timed "someone needs help" signal somewhere in the system. Reach it before it goes quiet for a black box and a
// little ore. world/distress_beacons provides it. docs/DISTRESS_BEACONS.md.
//
//     auto* db = eng.services.get<world::IDistressBeacons>();   // null if the module is off (or distress_beacons.enabled = false)
//     if (db && db->detected()) radar.marker(db->position());
//     if (db && db->active()) hud.text("DISTRESS SIGNAL %.0fs", db->etaExpirySeconds());
//
// At most one beacon at a time. No perk needed (it is a broadcast); no save (a reload simply re-rolls the next timer from the seed).
#include <cstdint>
#include <string>
#include "world/star_system/star_system_api.h"   // world::Vec3d

namespace world {

class IDistressBeacons {
public:
    virtual ~IDistressBeacons() = default;
    virtual bool active() const = 0;               // a beacon is broadcasting right now
    virtual Vec3d position() const = 0;            // its current world position (moves with its body when near one); only valid while active()
    virtual float etaExpirySeconds() const = 0;    // seconds until it goes quiet, -1 when none is active
    virtual bool detected() const = 0;             // active and within distress_beacons.detect_range of the ship (what a radar marker shows)
};

// ---- events (engine.events) ----
struct DistressBeaconSpawned { uint32_t siteSeed = 0; double x = 0, y = 0, z = 0; };   // once, the step a beacon appears (world position then)
struct DistressBeaconInvestigated { std::string oreGiven; int amount = 0; };           // once, when the ship reaches it; amount = what the hold accepted
struct DistressBeaconExpired {};                                                        // once, when an unclaimed beacon's lifetime runs out

} // namespace world
