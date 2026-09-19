#pragma once
// Collision detection between spheres, with events. It DETECTS; the module that owns a body decides what a hit means
// (damage, bounce, destroy). Replaces the old "everything vs everything" checks with a spatial grid.
//
//     auto* phys = eng.services.get<core::IPhysics>();
//     core::BodyId id = phys->addBody("asteroid", pos, radius, /*dynamic=*/false);
//     // a moving body: each fixed step, after moving it
//     phys->setBody(shipId, pos, vel);
//     eng.events.subscribe<core::Collided>([&](const core::Collided& c) { if (c.kindA == "ship") ... });
//
// Movement is SWEPT (the path since the last step is tested), so fast bodies cannot tunnel through small ones.
// A Collided event fires when two bodies START touching, not every step they stay in contact.
#include <string>
#include <vector>
#include "engine/math.h"

namespace core {

using BodyId = int;
constexpr BodyId kNoBody = -1;

struct Collided {
    BodyId a = kNoBody, b = kNoBody;
    std::string kindA, kindB;
    engine::Vec3 normal;          // unit vector from a to b at the moment of contact
    float speed = 0;              // closing speed along the normal (0 if they are separating), m/s
    engine::Vec3 posA, posB;      // where each body was at the moment of contact
    float radiusA = 0, radiusB = 0;
};

class IPhysics {
public:
    virtual ~IPhysics() = default;
    // dynamic bodies are the movers (they can hit anything); static bodies only get hit and never collide with each other
    virtual BodyId addBody(const std::string& kind, const engine::Vec3& pos, float radius, bool dynamic) = 0;
    virtual void removeBody(BodyId id) = 0;
    virtual bool alive(BodyId id) const = 0;
    // Once per fixed step, AFTER moving the body: its path since the last step is swept for hits.
    virtual void setBody(BodyId id, const engine::Vec3& pos, const engine::Vec3& vel) = 0;
    // Jump somewhere (load a game, respawn, resolve a collision) without sweeping through everything in between.
    virtual void teleport(BodyId id, const engine::Vec3& pos) = 0;
    // Bodies whose sphere overlaps the given sphere (O(n): fine for occasional queries).
    virtual void query(const engine::Vec3& center, float radius, std::vector<BodyId>& out) const = 0;
};

} // namespace core
