#include <cmath>
#include <set>
#include "tests/test.h"
#include "world/star_system/planet_mesh.h"

namespace {
bool close(float a, float b, float tol = 1e-4f) { return std::fabs(a - b) <= tol; }
world::PlanetParams params(uint32_t seed) { world::PlanetParams p; p.seed = seed; return p; }
} // namespace

TEST(planet_mesh_counts_per_level) {
    for (int l = 0; l <= 4; l++) {
        auto m = world::buildPlanetMesh(l, params(5));
        CHECK_EQ(m.vertexCount(), 10 * (1 << (2 * l)) + 2);
        CHECK_EQ(m.triangleCount(), 20 * (1 << (2 * l)));
        CHECK_EQ(m.vertexCount(), world::meshVertexCount(l));
        CHECK_EQ(m.triangleCount(), world::meshTriangleCount(l));
        for (auto i : m.indices) CHECK((int)i < m.vertexCount());
    }
    CHECK_EQ(world::buildPlanetMesh(9, params(1)).level, 4);       // clamped
    CHECK_EQ(world::buildPlanetMesh(-3, params(1)).level, 0);
}

TEST(planet_mesh_vertices_on_radius_within_amplitude_and_normals_unit) {
    for (uint32_t seed : {1u, 2u, 77u, 123456u}) {
        world::PlanetParams p = params(seed); p.terrainHeight = 0.03f;
        auto m = world::buildPlanetMesh(3, p);
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < m.vertexCount(); i++) {
            const float* v = &m.verts[i * 9];
            float r = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            lo = std::min(lo, r); hi = std::max(hi, r);
            CHECK(r >= 1.0f - 0.03f - 1e-4f && r <= 1.0f + 0.03f + 1e-4f);
            float nl = std::sqrt(v[3] * v[3] + v[4] * v[4] + v[5] * v[5]);
            CHECK(close(nl, 1.0f, 1e-3f));
            // normals point outward
            CHECK(v[0] * v[3] + v[1] * v[4] + v[2] * v[5] > 0.5f);
            for (int k = 6; k < 9; k++) CHECK(v[k] >= 0.0f && v[k] <= 1.0f);    // colours are valid
            for (int k = 0; k < 9; k++) CHECK(v[k] == v[k]);                     // no NaN
        }
        CHECK(hi - lo > 0.005f);                                                  // there is actual relief
    }
    world::PlanetParams flat = params(3); flat.terrainHeight = 0.0f;              // no terrain: a perfect sphere
    auto m = world::buildPlanetMesh(2, flat);
    for (int i = 0; i < m.vertexCount(); i++) { const float* v = &m.verts[i * 9]; CHECK(close(std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), 1.0f, 1e-3f)); }
}

TEST(planet_mesh_is_deterministic_and_seed_dependent) {
    auto a = world::buildPlanetMesh(3, params(42)), b = world::buildPlanetMesh(3, params(42)), c = world::buildPlanetMesh(3, params(43));
    CHECK_EQ(a.verts.size(), b.verts.size());
    CHECK(a.indices == b.indices);
    float maxDiffSame = 0, maxDiffOther = 0;
    for (size_t i = 0; i < a.verts.size(); i++) { maxDiffSame = std::max(maxDiffSame, std::fabs(a.verts[i] - b.verts[i])); maxDiffOther = std::max(maxDiffOther, std::fabs(a.verts[i] - c.verts[i])); }
    CHECK(maxDiffSame < 1e-6f);
    CHECK(maxDiffOther > 1e-3f);
    // the same direction has the same height at every level (levels nest): vertex 0 of the icosahedron survives subdivision unchanged
    auto lo = world::buildPlanetMesh(1, params(42)), hi = world::buildPlanetMesh(4, params(42));
    for (int k : {0, 1, 2, 6, 7, 8}) CHECK(close(lo.verts[k], hi.verts[k], 1e-5f));   // position and colour (normals depend on the neighbours)
}

TEST(planet_mesh_noise_is_in_range_and_smooth) {
    float lo = 1, hi = 0;
    for (int i = 0; i < 2000; i++) {
        float x = i * 0.137f, y = i * 0.071f - 3.0f, z = i * 0.213f;
        float n = world::fbm(x, y, z, 9, 5);
        CHECK(n >= 0.0f && n <= 1.0f);
        lo = std::min(lo, n); hi = std::max(hi, n);
    }
    CHECK(hi - lo > 0.3f);
    CHECK(std::fabs(world::fbm(1.0f, 2.0f, 3.0f, 4, 4) - world::fbm(1.001f, 2.0f, 3.0f, 4, 4)) < 0.01f);   // continuous
}

TEST(planet_mesh_biomes_ocean_is_flat_and_poles_are_snowy) {
    world::PlanetParams p = params(1);
    while (!world::planetHasOcean(p)) p.seed++;
    auto m = world::buildPlanetMesh(4, p);
    int flat = 0;
    for (int i = 0; i < m.vertexCount(); i++) {
        const float* v = &m.verts[i * 9];
        float r = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (close(r, 1.0f + p.terrainHeight * (world::kSeaLevel * 2.0f - 1.0f), 1e-4f)) flat++;
    }
    CHECK(flat > 50);                                                   // some vertices are at sea level
    float c[3];
    world::biomeColor(p, true, 0.5f, 1.0f, c);                          // pole: snow (bright)
    CHECK(c[0] > 0.7f && c[1] > 0.7f && c[2] > 0.7f);
    world::biomeColor(p, true, 0.1f, 0.0f, c);                          // deep equatorial ocean: bluish, dark
    CHECK(c[2] > c[0] && c[0] < 0.5f);
    world::PlanetParams moon = params(1); moon.moon = true;
    CHECK(!world::planetHasOcean(moon));
}

TEST(planet_lod_choice_scales_with_size_and_has_hysteresis) {
    float far = world::wantedLevel(3.0f, 12.0f), mid = world::wantedLevel(40.0f, 12.0f), near = world::wantedLevel(400.0f, 12.0f);
    CHECK(far < mid && mid < near);
    CHECK_EQ(world::chooseLod(far, -1, 4), 0);
    CHECK_EQ(world::chooseLod(near, -1, 4), 4);                        // capped at the max level
    CHECK_EQ(world::chooseLod(near, -1, 3), 3);
    CHECK_EQ(world::chooseLod(near, -1, 0), 0);
    // hysteresis: hovering around the boundary between level 2 and 3 must not flip every frame
    int cur = 2; int flips = 0;
    float w[] = {2.05f, 1.95f, 2.1f, 1.9f, 2.2f, 1.8f, 2.25f, 1.85f};
    for (float x : w) { int n = world::chooseLod(x, cur, 4); if (n != cur) flips++; cur = n; }
    CHECK_EQ(flips, 0);
    CHECK_EQ(world::chooseLod(2.5f, 2, 4), 3);                         // clearly bigger: up
    CHECK_EQ(world::chooseLod(0.5f, 3, 4), 1);                         // clearly smaller: down (wanted < 2 - h)
    // pixel size
    CHECK(close(world::pixelRadius(400, 4000, 720, 90), 400.0f / 4000.0f * 360.0f, 1e-2f));
    CHECK(world::pixelRadius(400, 300, 720, 90) > 1e5f);                // inside the body
}

TEST(planet_lod_budget_is_respected_and_drops_the_least_important_first) {
    std::vector<int> lv = {4, 4, 4, 4, 4};
    std::vector<float> px = {500, 300, 100, 30, 10};
    int t = world::applyTriangleBudget(lv, px, 20000);
    CHECK(t <= 20000);
    int sum = 0; for (int l : lv) sum += world::meshTriangleCount(l);
    CHECK_EQ(sum, t);
    CHECK(lv[0] >= lv[4]);                                              // the biggest on screen keeps at least as much detail as the smallest
    CHECK(lv[0] >= 3);
    std::vector<int> ok = {2, 1};
    CHECK_EQ(world::applyTriangleBudget(ok, {50, 20}, 100000), world::meshTriangleCount(2) + world::meshTriangleCount(1));   // fits: untouched
    std::vector<int> tight = {3, 3};
    int tt = world::applyTriangleBudget(tight, {50, 20}, 1);            // impossible budget: bottoms out at level 0
    CHECK_EQ(tight[0], 0); CHECK_EQ(tight[1], 0); CHECK_EQ(tt, 40);
}
