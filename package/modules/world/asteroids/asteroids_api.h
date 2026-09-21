#pragma once
// Read-only view of the asteroid field, for radar / mining / navigation (Phase 4.3). world/asteroids provides it.
//
//     auto* ast = eng.services.get<world::IAsteroids>();          // null if the module is off
//     std::vector<int> near; ast->nearest({x, y, z}, 10, near);   // the 10 closest, nearest first
//     for (int i : near) { ast->position(i); ast->radius(i); ast->ore(i); }
//
// Asteroids are STATIC (a belt is a shape, not a simulation) and never removed, so an index stays valid for the whole run.
#include <string>
#include <vector>
#include "world/star_system/star_system_api.h"   // world::Vec3d

namespace world {

class IAsteroids {
public:
    virtual ~IAsteroids() = default;
    virtual int count() const = 0;
    virtual Vec3d position(int i) const = 0;
    virtual float radius(int i) const = 0;
    virtual std::string ore(int i) const = 0;                               // ore id from data/ores.json ("rock" without data)
    virtual void nearest(const Vec3d& p, int n, std::vector<int>& out) const = 0;   // O(count): for occasional use
};

} // namespace world
