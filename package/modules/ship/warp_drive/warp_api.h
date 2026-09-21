#pragma once
// The warp drive's public face, for the inventory / crafting UI (Phase 5), the HUD and the starfield streaks.
//
//     auto* drive = eng.services.get<ship::IWarpDrive>();    // null if ship/warp_drive is off
//     drive->maxSpeed();                                      // m/s, base speed x the level's multiplier
//     drive->setLevel(drive->level() + 1);                    // an upgrade (clamped to the levels in data/warp_drive.json)
#include <string>

namespace ship {

class IWarpDrive {
public:
    virtual ~IWarpDrive() = default;
    virtual int level() const = 0;                 // current upgrade level, 0-based
    virtual int levelCount() const = 0;            // how many levels the table has
    virtual float maxSpeed() const = 0;            // effective warp speed cap, m/s
    virtual void setLevel(int level) = 0;          // clamped to 0..levelCount()-1; takes effect immediately
    virtual bool engaged() const = 0;
};

} // namespace ship
