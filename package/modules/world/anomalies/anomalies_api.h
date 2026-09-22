#pragma once
// Read-only view of the anomaly sites, for the cockpit radar (and later the system map). world/anomalies provides it. docs/ANOMALIES.md.
//
//     auto* an = eng.services.get<world::IAnomalies>();          // null if the module is off
//     for (int i = 0; i < an->count(); i++) if (an->detected(i)) an->position(i);
//
// Sites are only DETECTED with the "anomaly_scanner" perk, inside the detect range, and until they are investigated.
#include <string>
#include "world/star_system/star_system_api.h"   // world::Vec3d

namespace world {

class IAnomalies {
public:
    virtual ~IAnomalies() = default;
    virtual int count() const = 0;                 // generated sites (investigated ones included)
    virtual Vec3d position(int i) const = 0;       // current world position (near-body sites move with their body)
    virtual bool detected(int i) const = 0;        // shown on the radar now (perk + in range + not investigated), updated every frame
    virtual bool investigated(int i) const = 0;
};

// Emitted once per site when the ship flies within anomalies.investigate_range (with the scanner perk).
struct AnomalyInvestigated {
    int siteId = -1;
    std::string kind;       // data/anomalies.json id
    std::string oreGiven;   // "" when the kind has no reward entry
    int amount = 0;         // what the hold actually accepted (0 without gameplay/inventory, or when that ore's hold is full)
    std::string blueprint;  // blueprint newly unlocked here ("" = none: the kind has none, gameplay/blueprints is off, or it was already known)
};

} // namespace world
