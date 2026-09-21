#pragma once
// The star system service. world/star_system provides it; radar, navigation, collisions (3.4) and others consume it.
//
//     auto* sys = eng.services.get<world::IStarSystem>();     // null if the module is off
//     for (const auto& b : sys->bodies()) { ... b.position (double), b.radius, b.kind ... }
//
// Positions are DOUBLES (the system spans hundreds of thousands of units). Convert to float only after subtracting a nearby origin.
#include <string>
#include <vector>

namespace world {

struct Vec3d { double x = 0, y = 0, z = 0; };

enum class BodyKind { Sun, Planet, Moon };

struct Body {
    int id = 0;                    // index in bodies(); the sun is 0; a parent always has a lower id than its moons
    std::string name;
    BodyKind kind = BodyKind::Planet;
    Vec3d position;                // current world position
    float radius = 1.0f;
    int parent = -1;               // id of the body it orbits (-1 for the sun)
    double orbitRadius = 0;        // distance from the parent, units
    double period = 0;             // seconds for one orbit (0 for the sun)
    float color[3] = {1, 1, 1};
    double phase = 0;              // orbit angle at t = 0, radians
    double tilt = 0;               // orbital plane inclination, radians
};

class IStarSystem {
public:
    virtual ~IStarSystem() = default;
    virtual unsigned seed() const = 0;
    virtual double simTime() const = 0;                     // seconds of simulated time (frozen while paused)
    virtual Vec3d sunPosition() const = 0;
    virtual const std::vector<Body>& bodies() const = 0;    // index 0 = sun
    virtual Vec3d positionAt(int id) const = 0;             // current position of a body (sun position if id is out of range)
};

} // namespace world
