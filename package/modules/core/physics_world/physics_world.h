#pragma once
// core/physics_world - implementation of core::IPhysics: uniform spatial grid + swept sphere tests.
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "core/physics_world/physics_api.h"
#include "engine/module.h"

namespace core {

class PhysicsWorld : public engine::Module, public IPhysics {
public:
    const char* name() const override { return "core/physics_world"; }
    int priority() const override { return 10; }   // runs its fixed step AFTER gameplay modules have moved their bodies
    bool init(engine::Engine&) override;
    void shutdown(engine::Engine&) override;
    void onFixedUpdate(engine::Engine&, float) override { step(); }

    using IPhysics::addBody;          // keep the float convenience overloads visible through this type (tests, callers holding a PhysicsWorld&)
    using IPhysics::setBody;
    using IPhysics::teleport;
    using IPhysics::query;
    BodyId addBody(const std::string& kind, const engine::Vec3d& pos, float radius, bool dynamic) override;
    void removeBody(BodyId id) override;
    bool alive(BodyId id) const override { return id >= 0 && id < (BodyId)bodies_.size() && bodies_[(size_t)id].alive; }
    void setBody(BodyId id, const engine::Vec3d& pos, const engine::Vec3& vel) override;
    void teleport(BodyId id, const engine::Vec3d& pos) override;
    void query(const engine::Vec3d& center, float radius, std::vector<BodyId>& out) const override;
    int aliveBodyCount() const override { return (int)liveCount_; }

    void step();                                   // one collision pass (called every fixed update; public for tests)
    size_t bodyCount() const { return liveCount_; }
    void setCellSize(float c) { cell_ = c > 1.0f ? c : 1.0f; }

private:
    struct Body {
        std::string kind;
        engine::Vec3d pos, prev;      // absolute positions: double (docs/PRECISION.md). Only differences are ever narrowed to float.
        engine::Vec3 vel;            // m/s: float is plenty
        float radius = 0;
        bool dynamic = false, alive = false;
    };
    static constexpr int kMaxCellsPerBody = 64;   // bigger bodies (planets, the sun) go in a short "big" list instead

    struct Cell { int x, y, z; };
    static uint64_t key(int x, int y, int z);
    int cellOf(double v) const;
    void bounds(const Body& b, Cell& lo, Cell& hi) const;
    void buildGrid();

    std::vector<Body> bodies_;
    std::vector<BodyId> freeIds_;
    size_t liveCount_ = 0;
    float cell_ = 200.0f;

    std::unordered_map<uint64_t, std::vector<BodyId>> grid_;
    std::vector<BodyId> big_;
    std::vector<uint32_t> stamp_;                  // per-body de-duplication of candidates
    uint32_t stampNow_ = 0;
    std::unordered_set<uint64_t> active_;          // pairs touching at the end of the last step
    engine::Engine* eng_ = nullptr;
};

} // namespace core
