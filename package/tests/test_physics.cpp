#include "core/physics_world/physics_world.h"
#include "engine/engine.h"
#include "tests/test.h"

using engine::Vec3;
using core::BodyId;

namespace {
struct PhysRig {
    engine::Engine eng;
    core::PhysicsWorld w;
    std::vector<core::Collided> hits;
    PhysRig() {
        w.setCellSize(50.0f);
        w.init(eng);
        eng.events.subscribe<core::Collided>([this](const core::Collided& c) { hits.push_back(c); });
    }
    // move a dynamic body and run one step
    void move(BodyId id, Vec3 pos, Vec3 vel = {}) { w.setBody(id, pos, vel); w.step(); }
};
} // namespace

TEST(physics_touching_emits_once_not_every_step) {
    PhysRig r;
    BodyId rock = r.w.addBody("asteroid", {10, 0, 0}, 3.0f, false);
    BodyId ship = r.w.addBody("ship", {0, 0, 0}, 1.0f, true);
    r.move(ship, {5, 0, 0});                 // gap 5 > 4: not touching
    CHECK_EQ(r.hits.size(), (size_t)0);
    r.move(ship, {7, 0, 0});                 // 3 < 4: contact begins
    CHECK_EQ(r.hits.size(), (size_t)1);
    r.move(ship, {7.5f, 0, 0});              // still touching: no repeat
    r.move(ship, {7.6f, 0, 0});
    CHECK_EQ(r.hits.size(), (size_t)1);
    CHECK_EQ(r.hits[0].a, ship);
    CHECK_EQ(r.hits[0].b, rock);
    CHECK_EQ(r.hits[0].kindA, std::string("ship"));
    CHECK_EQ(r.hits[0].kindB, std::string("asteroid"));
    r.move(ship, {0, 0, 0});                 // leave (teleport-like big move away; still swept, so they touched on the way out: state reset)
    r.w.teleport(ship, {0, 0, 0});
    r.w.step();
    r.hits.clear();
    r.move(ship, {7, 0, 0});                 // approach again: a NEW contact event
    CHECK_EQ(r.hits.size(), (size_t)1);
}

TEST(physics_event_geometry_normal_speed_contact_positions) {
    PhysRig r;
    BodyId rock = r.w.addBody("asteroid", {10, 0, 0}, 3.0f, false);
    BodyId ship = r.w.addBody("ship", {0, 0, 0}, 1.0f, true);
    r.move(ship, {6.5f, 0, 0}, {20, 0, 0});
    CHECK_EQ(r.hits.size(), (size_t)1);
    auto& h = r.hits[0];
    CHECK(std::abs(h.normal.x - 1.0f) < 1e-4f && std::abs(h.normal.y) < 1e-4f);   // from ship toward rock
    CHECK(std::abs(h.speed - 20.0f) < 1e-3f);                                       // closing speed along the normal
    CHECK_EQ(h.radiusA, 1.0f);
    CHECK_EQ(h.radiusB, 3.0f);
    CHECK(std::abs(engine::length(h.posB - h.posA)) <= 4.0f + 1e-3f);              // at contact they are within the radii sum
    (void)rock;
}

TEST(physics_separating_bodies_report_zero_speed) {
    PhysRig r;
    r.w.addBody("asteroid", {10, 0, 0}, 3.0f, false);
    BodyId ship = r.w.addBody("ship", {0, 0, 0}, 1.0f, true);
    r.move(ship, {7, 0, 0}, {-30, 0, 0});    // overlapping but moving away
    CHECK_EQ(r.hits.size(), (size_t)1);
    CHECK_EQ(r.hits[0].speed, 0.0f);
}

TEST(physics_fast_bodies_cannot_tunnel_through_small_ones) {
    PhysRig r;
    r.w.addBody("mine", {60, 0, 0}, 1.0f, false);          // tiny target
    BodyId ship = r.w.addBody("ship", {0, 0, 0}, 1.0f, true);
    r.move(ship, {120, 0, 0}, {7200, 0, 0});               // one step covers 120 m: start and end are far from the mine
    CHECK_EQ(r.hits.size(), (size_t)1);                    // ...but the swept path went straight through it
    CHECK(std::abs(r.hits[0].posA.x - 58.0f) < 0.05f);     // first touch at x = 60 - (1+1), not the closest approach at 60
    CHECK(std::abs(engine::length(r.hits[0].posB - r.hits[0].posA) - 2.0f) < 0.05f);   // exactly the radii sum apart
}

TEST(physics_teleport_does_not_sweep) {
    PhysRig r;
    r.w.addBody("wall", {60, 0, 0}, 1.0f, false);
    BodyId ship = r.w.addBody("ship", {0, 0, 0}, 1.0f, true);
    r.w.teleport(ship, {120, 0, 0});                       // e.g. a respawn: jumping over things is not a collision
    r.w.step();
    CHECK_EQ(r.hits.size(), (size_t)0);
}

TEST(physics_static_pairs_never_collide_and_dynamic_pairs_report_once) {
    PhysRig r;
    r.w.addBody("rock", {0, 0, 0}, 5.0f, false);
    r.w.addBody("rock", {3, 0, 0}, 5.0f, false);           // overlapping statics: ignored
    r.w.step();
    CHECK_EQ(r.hits.size(), (size_t)0);
    BodyId a = r.w.addBody("ship", {100, 0, 0}, 2.0f, true);
    BodyId b = r.w.addBody("ship", {110, 0, 0}, 2.0f, true);
    r.w.setBody(a, {104, 0, 0}, {}); r.w.setBody(b, {106, 0, 0}, {});
    r.w.step();
    CHECK_EQ(r.hits.size(), (size_t)1);                    // one event for the pair, not two
    CHECK_EQ(r.hits[0].a, a);
}

TEST(physics_big_bodies_span_cells_and_are_found_from_anywhere_on_them) {
    PhysRig r;
    r.w.addBody("planet", {0, 0, 0}, 1000.0f, false);      // radius 20 cells of 50 m: goes in the "big" list
    BodyId ship = r.w.addBody("ship", {2000, 0, 0}, 2.0f, true);
    r.move(ship, {2000, 0, 0});
    CHECK_EQ(r.hits.size(), (size_t)0);
    r.move(ship, {1201, 0, 0});                            // still 199 m above the surface
    CHECK_EQ(r.hits.size(), (size_t)0);
    r.move(ship, {1001, 0, 0});                            // short hop (few cells) onto the surface, 1000 m from the centre
    CHECK_EQ(r.hits.size(), (size_t)1);
    CHECK_EQ(r.hits[0].kindB, std::string("planet"));
}

TEST(physics_only_nearby_bodies_are_hit_among_thousands) {
    PhysRig r;
    for (int i = 0; i < 3000; i++) r.w.addBody("rock", {(float)(i % 60) * 30.0f, (float)(i / 60) * 30.0f + 400.0f, 0}, 2.0f, false);
    BodyId near = r.w.addBody("rock", {5000, 5000, 5000}, 3.0f, false);
    BodyId ship = r.w.addBody("ship", {4990, 5000, 5000}, 2.0f, true);
    r.move(ship, {4995, 5000, 5000});
    CHECK_EQ(r.hits.size(), (size_t)1);
    CHECK_EQ(r.hits[0].b, near);
    CHECK_EQ(r.w.bodyCount(), (size_t)3002);
}

TEST(physics_remove_and_id_reuse_are_clean) {
    PhysRig r;
    BodyId rock = r.w.addBody("rock", {10, 0, 0}, 3.0f, false);
    BodyId ship = r.w.addBody("ship", {0, 0, 0}, 1.0f, true);
    r.move(ship, {7, 0, 0});
    CHECK_EQ(r.hits.size(), (size_t)1);
    r.w.removeBody(rock);
    CHECK(!r.w.alive(rock));
    CHECK_EQ(r.w.bodyCount(), (size_t)1);
    r.hits.clear();
    r.move(ship, {8, 0, 0});
    CHECK_EQ(r.hits.size(), (size_t)0);                    // removed: no events
    BodyId rock2 = r.w.addBody("rock2", {12, 0, 0}, 3.0f, false);
    CHECK_EQ(rock2, rock);                                 // id was reused
    r.move(ship, {8.5f, 0, 0});
    CHECK_EQ(r.hits.size(), (size_t)1);                    // the new occupant of the id is a fresh contact
    CHECK_EQ(r.hits[0].kindB, std::string("rock2"));
    r.w.removeBody(999);                                   // bogus ids are harmless
    r.w.setBody(999, {}, {});
    r.w.teleport(-1, {});
}

TEST(physics_query_finds_overlapping_bodies) {
    PhysRig r;
    BodyId a = r.w.addBody("a", {0, 0, 0}, 1.0f, false);
    r.w.addBody("b", {100, 0, 0}, 1.0f, false);
    BodyId c = r.w.addBody("c", {6, 0, 0}, 2.0f, true);
    std::vector<BodyId> out;
    r.w.query({0, 0, 0}, 5.0f, out);
    CHECK_EQ(out.size(), (size_t)2);
    CHECK((out[0] == a && out[1] == c));
}

TEST(physics_handler_may_teleport_the_body_it_was_told_about) {
    PhysRig r;
    r.w.addBody("rock", {10, 0, 0}, 3.0f, false);
    BodyId ship = r.w.addBody("ship", {0, 0, 0}, 1.0f, true);
    r.eng.events.subscribe<core::Collided>([&](const core::Collided& c) {
        r.w.teleport(c.a, c.posB - c.normal * (c.radiusA + c.radiusB + 0.01f));   // resolve: put it just outside
    });
    r.move(ship, {8, 0, 0});
    CHECK_EQ(r.hits.size(), (size_t)1);
    r.move(ship, {5.99f, 0, 0}, {});                       // resting just outside: sweep starts outside, no repeat
    CHECK_EQ(r.hits.size(), (size_t)1);
}

TEST(physics_alive_body_count_tracks_add_and_remove) {
    PhysRig r;
    CHECK_EQ(r.w.aliveBodyCount(), 0);
    BodyId a = r.w.addBody("a", {0, 0, 0}, 1.0f, false);
    BodyId b = r.w.addBody("b", {50, 0, 0}, 1.0f, true);
    CHECK_EQ(r.w.aliveBodyCount(), 2);
    r.w.removeBody(a);
    CHECK_EQ(r.w.aliveBodyCount(), 1);
    r.w.removeBody(b);
    CHECK_EQ(r.w.aliveBodyCount(), 0);
}

TEST(physics_interface_default_alive_body_count_is_unsupported) {
    struct Minimal : core::IPhysics {   // an IPhysics implementer that never overrides the new method still compiles and reports "unknown"
        core::BodyId addBody(const std::string&, const engine::Vec3d&, float, bool) override { return 0; }
        void removeBody(core::BodyId) override {}
        bool alive(core::BodyId) const override { return false; }
        void setBody(core::BodyId, const engine::Vec3d&, const engine::Vec3&) override {}
        void teleport(core::BodyId, const engine::Vec3d&) override {}
        void query(const engine::Vec3d&, float, std::vector<core::BodyId>&) const override {}
    } m;
    CHECK_EQ(m.aliveBodyCount(), -1);
}
