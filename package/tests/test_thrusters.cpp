// cockpit::thrusterJets (model_thrusters.h): '@THRUST-JET' tagged faces -> exhaust emitters.
#include <cmath>
#include "ship/cockpit/model_thrusters.h"
#include "tests/test.h"

namespace {
bool nearT(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }
using P = cockpit::JetPolygon;
// a 1 x 1 square in the plane z = zc around (xc, yc), counter-clockwise seen from +z (so its winding normal is +z)
P squareZ(float xc, float yc, float zc, bool ccwFromPlusZ = true) {
    engine::Vec3 a{xc - 0.5f, yc - 0.5f, zc}, b{xc + 0.5f, yc - 0.5f, zc}, c{xc + 0.5f, yc + 0.5f, zc}, d{xc - 0.5f, yc + 0.5f, zc};
    return ccwFromPlusZ ? P{a, b, c, d} : P{a, d, c, b};
}
const engine::Vec3 kHull{0, 0, 0};
} // namespace

TEST(thrusters_no_tag_is_empty) {
    CHECK(cockpit::thrusterJets({}, kHull).empty());
    const char* obj = "v 0 0 8\nv 1 0 8\nv 1 1 8\nusemtl @HUD-INFO\nf 1 2 3\nusemtl BODY\nf 1 2 3\n";
    CHECK(cockpit::jetPolygonsFromObj(obj).empty());
}

TEST(thrusters_obj_faces_read_as_written) {
    const char* obj = "v 0 0 8\nv 1 0 8\nv 1 1 8\nv 0 1 8\nusemtl BODY\nf 1 2 3\nusemtl @THRUST-JET\nf 1/1/1 2/2/1 3/3/1\nf -4//1 -3//1 -2//1 -1//1\n"
                      "f 1 2 99\nusemtl BODY\nf 1 2 3 4\n";
    auto p = cockpit::jetPolygonsFromObj(obj);
    CHECK(p.size() == 2);                        // the bad index face is skipped, BODY faces are not jets
    CHECK(p[0].size() == 3 && p[1].size() == 4);
    CHECK(nearT(p[1][3].y, 1.0f));
}

TEST(thrusters_quad_centre_normal_radius) {
    auto j = cockpit::thrusterJets({squareZ(0.5f, -0.4f, 8.0f)}, kHull);
    CHECK(j.size() == 1);
    CHECK(nearT(j[0].position.x, 0.5f) && nearT(j[0].position.y, -0.4f) && nearT(j[0].position.z, 8.0f));
    CHECK(nearT(j[0].direction.z, 1.0f) && nearT(j[0].direction.x, 0) && nearT(j[0].direction.y, 0));
    CHECK(nearT(j[0].radius, std::sqrt(0.5f), 1e-3f));
}

TEST(thrusters_normal_follows_winding_then_points_away_from_hull) {
    // a jet facing forward (-z) at the nose: counter-clockwise from -z; hull centre behind it
    auto front = cockpit::thrusterJets({squareZ(0, 0, -5, false)}, {0, 0, 0});
    CHECK(front.size() == 1 && nearT(front[0].direction.z, -1.0f));
    // wound the "wrong" way (winding says -z) on a rear jet: flipped to point away from the body (+z)
    auto rear = cockpit::thrusterJets({squareZ(0, 0, 8, false)}, {0, 0, 2});
    CHECK(rear.size() == 1 && nearT(rear[0].direction.z, 1.0f));
}

TEST(thrusters_triangle_counts_only_three_corners) {
    // triangle (0,0,8) (1,0,8) (0,1,8): centroid (1/3, 1/3), normal +z
    auto j = cockpit::thrusterJets({P{{0, 0, 8}, {1, 0, 8}, {0, 1, 8}}}, kHull);
    CHECK(j.size() == 1);
    CHECK(nearT(j[0].position.x, 1.0f / 3) && nearT(j[0].position.y, 1.0f / 3) && nearT(j[0].direction.z, 1.0f));
}

TEST(thrusters_triangle_fan_disc_is_one_jet) {
    // an 8-triangle fan around (0,-0.4,7.8), radius 0.9 (ShipV3's jet shape)
    std::vector<P> fan;
    const float cx = 0, cy = -0.4f, z = 7.8f, r = 0.9f;
    for (int i = 0; i < 8; i++) {
        float a0 = (float)i * 0.785398f, a1 = (float)(i + 1) * 0.785398f;
        fan.push_back(P{{cx, cy, z}, {cx + r * std::cos(a0), cy + r * std::sin(a0), z}, {cx + r * std::cos(a1), cy + r * std::sin(a1), z}});
    }
    auto j = cockpit::thrusterJets(fan, {0, 0, 3});
    CHECK(j.size() == 1);
    CHECK(nearT(j[0].position.x, cx, 1e-3f) && nearT(j[0].position.y, cy, 1e-3f) && nearT(j[0].position.z, z, 1e-3f));
    CHECK(nearT(j[0].direction.z, 1.0f));
    CHECK(nearT(j[0].radius, r, 1e-3f));
}

TEST(thrusters_degenerate_faces_are_skipped) {
    P line{{0, 0, 8}, {1, 0, 8}, {2, 0, 8}, {1, 0, 8}};
    P point{{1, 1, 8}, {1, 1, 8}, {1, 1, 8}};
    CHECK(cockpit::thrusterJets({line, point}, kHull).empty());
    auto j = cockpit::thrusterJets({line, squareZ(3, 0, 8)}, kHull);   // the good one survives, the line does not pull its centre
    CHECK(j.size() == 1 && nearT(j[0].position.x, 3.0f));
}

TEST(thrusters_far_apart_faces_are_separate_emitters) {
    // two engines 6 m apart, each made of two adjacent quads
    std::vector<P> faces{squareZ(-3, 0, 8), squareZ(-3, 1, 8), squareZ(3, 0, 8), squareZ(3, 1, 8)};
    auto j = cockpit::thrusterJets(faces, {0, 0, 2});
    CHECK(j.size() == 2);
    float xs[2] = {j[0].position.x, j[1].position.x};
    CHECK((nearT(xs[0], -3) && nearT(xs[1], 3)) || (nearT(xs[0], 3) && nearT(xs[1], -3)));
    for (auto& e : j) { CHECK(nearT(e.position.y, 0.5f)); CHECK(nearT(e.direction.z, 1.0f)); }
    // a small cluster distance cannot split faces that share an edge
    CHECK(cockpit::thrusterJets(faces, {0, 0, 2}, 0.01f).size() == 2);
}
