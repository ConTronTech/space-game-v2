#pragma once
// The built-in "HUD" screen group: FLIGHT_DATA, SHIP_SYSTEMS and PROXIMITY_RADAR (the three screens of ShipV2).
// Content comes only through ship::IShip; without it every screen says NO DATA (and one warning is logged).
#include <string>
#include "engine/engine.h"
#include <vector>
#include "ship/cockpit/cockpit_screens_api.h"
#include "ship/cockpit/screen_rate.h"
#include "ship/ship_core/ship_api.h"

namespace cockpit {

class HudScreens {
public:
    explicit HudScreens(engine::Engine& eng);
    void draw(const ScreenContext& ctx);

private:
    void flightData(const ScreenContext& ctx, const ship::IShip& ship);
    void shipSystems(const ScreenContext& ctx, const ship::IShip& ship);
    void proximityRadar(const ScreenContext& ctx);
    void noData(const ScreenContext& ctx);

    engine::Engine& eng_;
    bool warnedNoShip_ = false;
    float range_ = 400000.0f;   // radar rim, units (cockpit.radar_range)
    float asteroidRange_ = 3000.0f;   // asteroids are only plotted this close (cockpit.radar_asteroid_range)
    ScreenRate rockRate_;             // the "nearest asteroids" query is O(count): repeat it only a few times a second
    std::vector<int> rockIds_;        // its result (capacity reused)
};

// ship.json "content" for a tag, or the default for the well-known ShipV2 names when ship.json does not say.
std::string hudContentFor(const std::string& content, const std::string& quadName);

} // namespace cockpit
