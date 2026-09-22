#include "core/physics_world/physics_world.h"
#include <algorithm>
#include <cmath>
#include "engine/engine.h"
#include "engine/log.h"

namespace core {

using engine::Vec3;
using engine::Vec3d;

// Small double helpers (the same camera-relative discipline the renderer uses: subtract in double, narrow only the difference).
namespace {
inline Vec3d sub(const Vec3d& a, const Vec3d& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline double dot(const Vec3d& a, const Vec3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double len(const Vec3d& a) { return std::sqrt(dot(a, a)); }
inline Vec3d lerpD(const Vec3d& a, const Vec3d& b, double t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }
}

bool PhysicsWorld::init(engine::Engine& eng) {
    eng_ = &eng;
    cell_ = std::max(1.0f, eng.config.get("physics.cell_size", 200.0f, "collision grid cell size in metres (about the size of a typical object x 2-4)"));
    eng.services.provide<IPhysics>(this);
    return true;
}

void PhysicsWorld::shutdown(engine::Engine& eng) { eng.services.withdraw<IPhysics>(); }

BodyId PhysicsWorld::addBody(const std::string& kind, const Vec3d& pos, float radius, bool dynamic) {
    Body b;
    b.kind = kind; b.pos = b.prev = pos; b.radius = std::max(0.0f, radius); b.dynamic = dynamic; b.alive = true;
    liveCount_++;
    if (!freeIds_.empty()) { BodyId id = freeIds_.back(); freeIds_.pop_back(); bodies_[(size_t)id] = std::move(b); return id; }
    bodies_.push_back(std::move(b));
    return (BodyId)bodies_.size() - 1;
}

void PhysicsWorld::removeBody(BodyId id) {
    if (!alive(id)) return;
    bodies_[(size_t)id] = Body{};
    freeIds_.push_back(id);
    liveCount_--;
    // forget pairs involving it, so a reused id starts fresh
    for (auto it = active_.begin(); it != active_.end();) {
        BodyId lo = (BodyId)(*it >> 32), hi = (BodyId)(*it & 0xffffffffu);
        it = (lo == id || hi == id) ? active_.erase(it) : std::next(it);
    }
}

void PhysicsWorld::setBody(BodyId id, const Vec3d& pos, const Vec3& vel) {
    if (!alive(id)) return;
    bodies_[(size_t)id].pos = pos;
    bodies_[(size_t)id].vel = vel;
}

void PhysicsWorld::teleport(BodyId id, const Vec3d& pos) {
    if (!alive(id)) return;
    bodies_[(size_t)id].pos = bodies_[(size_t)id].prev = pos;
}

void PhysicsWorld::query(const Vec3d& c, float r, std::vector<BodyId>& out) const {
    out.clear();
    for (size_t i = 0; i < bodies_.size(); i++) {
        const Body& b = bodies_[i];
        if (b.alive && len(sub(b.pos, c)) <= (double)r + b.radius) out.push_back((BodyId)i);
    }
}

// ---- grid ----
uint64_t PhysicsWorld::key(int x, int y, int z) {
    return ((uint64_t)(x & 0x1FFFFF) << 42) | ((uint64_t)(y & 0x1FFFFF) << 21) | (uint64_t)(z & 0x1FFFFF);
}
int PhysicsWorld::cellOf(double v) const { return (int)std::floor(v / cell_); }

// grid cells covered by the body's swept volume (previous -> current position, plus radius)
void PhysicsWorld::bounds(const Body& b, Cell& lo, Cell& hi) const {
    lo = {cellOf(std::min(b.prev.x, b.pos.x) - b.radius), cellOf(std::min(b.prev.y, b.pos.y) - b.radius), cellOf(std::min(b.prev.z, b.pos.z) - b.radius)};
    hi = {cellOf(std::max(b.prev.x, b.pos.x) + b.radius), cellOf(std::max(b.prev.y, b.pos.y) + b.radius), cellOf(std::max(b.prev.z, b.pos.z) + b.radius)};
}

void PhysicsWorld::buildGrid() {
    grid_.clear();
    big_.clear();
    for (size_t i = 0; i < bodies_.size(); i++) {
        const Body& b = bodies_[i];
        if (!b.alive) continue;
        Cell lo, hi;
        bounds(b, lo, hi);
        long long count = (long long)(hi.x - lo.x + 1) * (hi.y - lo.y + 1) * (hi.z - lo.z + 1);
        if (count > kMaxCellsPerBody) { big_.push_back((BodyId)i); continue; }
        for (int x = lo.x; x <= hi.x; x++)
            for (int y = lo.y; y <= hi.y; y++)
                for (int z = lo.z; z <= hi.z; z++) grid_[key(x, y, z)].push_back((BodyId)i);
    }
}

// ---- one collision pass ----
void PhysicsWorld::step() {
    buildGrid();
    if (stamp_.size() < bodies_.size()) stamp_.resize(bodies_.size(), 0);

    std::vector<Collided> events;
    std::unordered_set<uint64_t> now;

    auto testPair = [&](BodyId ia, BodyId ic) {
        const Body& A = bodies_[(size_t)ia];
        const Body& C = bodies_[(size_t)ic];
        // Motion of A relative to C over the step is a straight line rel0 -> rel1. Find the FIRST time t in [0,1] when the
        // distance equals the radii sum (solve |rel0 + t d|^2 = R^2), so the contact point is where they first touched.
        // In DOUBLE: the two absolute positions are subtracted first, so the relative motion keeps full sub-millimetre accuracy however far
        // from the origin the pair is (in float at 5e9 the difference alone was already hundreds of metres out).
        Vec3d rel0 = sub(A.prev, C.prev), rel1 = sub(A.pos, C.pos), d = sub(rel1, rel0);
        double R = (double)A.radius + C.radius;
        double c = dot(rel0, rel0) - R * R;
        double t = 0.0;
        if (c > 0.0) {                                       // not overlapping at the start of the step
            double a = dot(d, d), bq = dot(rel0, d);
            if (a < 1e-12) return;                           // no relative motion, still apart
            double disc = bq * bq - a * c;
            if (disc < 0.0) return;                          // path never gets close enough
            t = (-bq - std::sqrt(disc)) / a;
            if (t < 0.0 || t > 1.0) return;                  // touches only before or after this step
        }

        Collided e;
        e.a = ia; e.b = ic; e.kindA = A.kind; e.kindB = C.kind;
        e.posAd = lerpD(A.prev, A.pos, t);
        e.posBd = lerpD(C.prev, C.pos, t);
        e.posA = {(float)e.posAd.x, (float)e.posAd.y, (float)e.posAd.z};   // float approximations for consumers that only draw / log them
        e.posB = {(float)e.posBd.x, (float)e.posBd.y, (float)e.posBd.z};
        Vec3d nd = sub(e.posBd, e.posAd);                                  // the contact normal is a DIFFERENCE: computed in double, then narrowed
        if (len(nd) < 1e-6) nd = len(d) > 1e-6 ? Vec3d{-d.x, -d.y, -d.z} : Vec3d{0, 1, 0};   // exactly overlapping: use the motion direction
        Vec3 n{(float)nd.x, (float)nd.y, (float)nd.z};
        e.normal = engine::normalize(n);
        e.speed = std::max(0.0f, engine::dot(A.vel - C.vel, e.normal));
        e.radiusA = A.radius; e.radiusB = C.radius;

        uint64_t pk = ((uint64_t)std::min(ia, ic) << 32) | (uint64_t)std::max(ia, ic);
        now.insert(pk);
        if (!active_.count(pk)) events.push_back(std::move(e));   // only when contact BEGINS
    };

    auto consider = [&](BodyId ia, BodyId ic) {
        if (ic == ia || !bodies_[(size_t)ic].alive) return;
        if (stamp_[(size_t)ic] == stampNow_) return;             // already tested against this mover
        stamp_[(size_t)ic] = stampNow_;
        // dynamic-vs-dynamic pairs are handled once, from the lower id
        if (bodies_[(size_t)ic].dynamic && ic < ia) return;
        testPair(ia, ic);
    };

    for (size_t i = 0; i < bodies_.size(); i++) {
        const Body& A = bodies_[i];
        if (!A.alive || !A.dynamic) continue;
        BodyId ia = (BodyId)i;
        if (++stampNow_ == 0) { std::fill(stamp_.begin(), stamp_.end(), 0); stampNow_ = 1; }

        Cell lo, hi;
        bounds(A, lo, hi);
        long long count = (long long)(hi.x - lo.x + 1) * (hi.y - lo.y + 1) * (hi.z - lo.z + 1);
        if (count > kMaxCellsPerBody) {                          // a huge mover: test against everything
            for (size_t c = 0; c < bodies_.size(); c++) consider(ia, (BodyId)c);
            continue;
        }
        for (int x = lo.x; x <= hi.x; x++)
            for (int y = lo.y; y <= hi.y; y++)
                for (int z = lo.z; z <= hi.z; z++) {
                    auto it = grid_.find(key(x, y, z));
                    if (it != grid_.end()) for (BodyId c : it->second) consider(ia, c);
                }
        for (BodyId c : big_) consider(ia, c);                   // planets/sun span too many cells to be in the grid
    }

    active_ = std::move(now);
    for (auto& b : bodies_) if (b.alive) b.prev = b.pos;         // next step sweeps from here

    // Emit AFTER the pass so handlers can freely teleport/remove bodies without disturbing the iteration above.
    if (eng_) for (auto& e : events) eng_->events.emit(e);
}

REGISTER_MODULE(PhysicsWorld);

} // namespace core
