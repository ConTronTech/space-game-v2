#pragma once
// Particle effects: ask for one by emitting an event, from any module.
//
//     eng.events.emit(fx::SpawnParticles{"spark", {x, y, z}, {0, 1, 0}, {vx, vy, vz}, 20});     // 20 sparks flying "up", carried along at (vx, vy, vz)
//
// `kind` names a preset (data/particles.json, see docs/FX.md): exhaust, spark, debris, warp_flash, muzzle, or your own. Unknown kinds are ignored.
// Nothing happens (and nothing costs anything) when fx/particles is off or fx.enabled is false.
#include <string>
#include "world/star_system/star_system_api.h"   // world::Vec3d

namespace fx {

struct SpawnParticles {
    std::string kind;                        // preset name
    world::Vec3d position;                   // world position (double)
    world::Vec3d direction{0, 0, 0};         // unit direction the burst points along; (0,0,0) = every direction
    world::Vec3d velocity{0, 0, 0};          // velocity of whatever spawned them: added to every particle (the ship's, a rock's)
    int count = 0;                           // 0 = the preset's own count range
    float size = 0;                          // 0 = the preset's size, else a multiplier of it
    float lifetime = 0;                      // 0 = the preset's lifetime, else a multiplier of it
    float colour[3] = {-1, -1, -1};          // r,g,b in 0..1; negative = the preset's own colours
    float radius = 0;                        // spawn position spread: a disc of this radius perpendicular to direction, e.g. an engine nozzle's face (0 = a single point)
};

} // namespace fx
