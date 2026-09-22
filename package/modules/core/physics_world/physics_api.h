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
#include <type_traits>
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
    engine::Vec3 posA, posB;      // where each body was at the moment of contact (float approximation of posAd/posBd)
    engine::Vec3d posAd, posBd;    // the same two points in double: a push-out far from the origin must start from these, not from posA/posB
    float radiusA = 0, radiusB = 0;
};

// Positions are DOUBLE inside the world (docs/PRECISION.md): a sphere test needs sub-metre accuracy in the DIFFERENCE of two positions, and
// far from the origin a float cannot hold one (at 5e9 one float step is 512 m). Each call has a double form and a float form; the float form
// is the old signature and simply widens, so a caller whose positions are small (or itself only has floats) needs no change.
// (The float forms are templates on purpose: a braced `{x, y, z}` argument cannot deduce a template parameter, so it is never ambiguous
// between the two - it always means the double form. A named engine::Vec3 still picks the float form.)
class IPhysics {
public:
    virtual ~IPhysics() = default;
    // dynamic bodies are the movers (they can hit anything); static bodies only get hit and never collide with each other
    virtual BodyId addBody(const std::string& kind, const engine::Vec3d& pos, float radius, bool dynamic) = 0;
    template <class V, class = std::enable_if_t<std::is_same_v<V, engine::Vec3>>>
    BodyId addBody(const std::string& kind, const V& pos, float radius, bool dynamic) { return addBody(kind, engine::Vec3d{pos.x, pos.y, pos.z}, radius, dynamic); }
    virtual void removeBody(BodyId id) = 0;
    virtual bool alive(BodyId id) const = 0;
    // Once per fixed step, AFTER moving the body: its path since the last step is swept for hits.
    virtual void setBody(BodyId id, const engine::Vec3d& pos, const engine::Vec3& vel) = 0;
    template <class V, class = std::enable_if_t<std::is_same_v<V, engine::Vec3>>>
    void setBody(BodyId id, const V& pos, const engine::Vec3& vel) { setBody(id, engine::Vec3d{pos.x, pos.y, pos.z}, vel); }
    // Jump somewhere (load a game, respawn, resolve a collision) without sweeping through everything in between.
    virtual void teleport(BodyId id, const engine::Vec3d& pos) = 0;
    template <class V, class = std::enable_if_t<std::is_same_v<V, engine::Vec3>>>
    void teleport(BodyId id, const V& pos) { teleport(id, engine::Vec3d{pos.x, pos.y, pos.z}); }
    // Bodies whose sphere overlaps the given sphere (O(n): fine for occasional queries).
    virtual void query(const engine::Vec3d& center, float radius, std::vector<BodyId>& out) const = 0;
    template <class V, class = std::enable_if_t<std::is_same_v<V, engine::Vec3>>>
    void query(const V& center, float radius, std::vector<BodyId>& out) const { query(engine::Vec3d{center.x, center.y, center.z}, radius, out); }

    // How many bodies are currently alive (for debug overlays/logging). -1 = this implementation does not track it.
    virtual int aliveBodyCount() const { return -1; }
};

} // namespace core
