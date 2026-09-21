#pragma once
// ship/docking: events and a service for the HUD / radar ("DOCK [G]" prompts).
//
//     eng.events.subscribe<ship::Docked>([](const ship::Docked& e) { /* e.stationName */ });
//     auto* dock = eng.services.get<ship::IDocking>();
//     std::string name, why; float dist; bool ok;
//     if (dock->nearestDockable(name, dist, ok, why)) { /* show name, distance, and "DOCK [G]" when ok, else the reason */ }
#include <string>

namespace ship {

struct Docked   { std::string stationName; };
struct Undocked { std::string stationName; };

class IDocking {
public:
    virtual ~IDocking() = default;
    virtual bool docked() const = 0;
    virtual std::string stationName() const = 0;        // the station we are docked at ("" when not docked)
    // The nearest station and whether pressing the dock key would work right now. Returns false when there is no station at all.
    // `distance` is from the station centre; `reason` says why not when `ok` is false ("too far from the station", "too fast", ...).
    virtual bool nearestDockable(std::string& name, float& distance, bool& ok, std::string& reason) const = 0;
};

} // namespace ship
