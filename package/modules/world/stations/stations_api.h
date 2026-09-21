#pragma once
// Read-only view of the space stations, for docking, radar and the HUD. world/stations provides it.
//
//     auto* st = eng.services.get<world::IStations>();          // null if the module is off
//     int i = st->nearest(shipPos);                              // -1 if there are none
//     world::StationInfo s = st->info(i);                        // position/velocity are current (they move with the parent body)
#include <string>
#include "world/star_system/star_system_api.h"   // world::Vec3d

namespace world {

enum class StationKind { Orbital, Planetary };

struct StationInfo {
    std::string name;
    StationKind kind = StationKind::Orbital;
    Vec3d position;              // centre of the model, world frame
    Vec3d velocity;              // m/s (the station moves with its parent planet, and orbital ones also around it)
    float radius = 120.0f;       // dock zone radius from the centre, units (scale * 60)
    float scale = 2.0f;
    float half = 20.0f;          // half extent of the model, units (also what the physics sphere is based on)
    Vec3d up;                    // unit vector: the surface normal (planetary) or the spin axis (orbital)
    int parent = -1;             // id of the planet in IStarSystem::bodies()
    // ---- orientation (added for landing-pad docking) ----
    // The model is a cube of half extent `half` centred on `position`, with a flat landing pad on its top face (along `up`).
    // Local axes: up (given above), forward (below), right = cross(up, forward). An orbital station spins about `up`; `forward` is the spin reference axis
    // at the CURRENT spin angle (it turns at spinRate). Planetary stations do not spin: spinRate = 0 and forward is fixed.
    Vec3d forward{0, 0, 1};
    double spinRate = 0;         // rad/s about `up` (right-hand rule), 0 for planetary stations
    float padTop = 23.2f;        // distance from `position` to the pad's top surface along `up`, units (1.16 x half)
    float padRadius = 30.0f;     // pad disc radius, units (1.5 x half): wider than the cube
};

class IStations {
public:
    virtual ~IStations() = default;
    virtual int count() const = 0;
    virtual StationInfo info(int i) const = 0;               // a default StationInfo for a bad index
    virtual int nearest(const Vec3d& p) const = 0;           // index of the station whose centre is nearest, -1 if none
};

} // namespace world
