#include <cmath>
#include <set>
#include "tests/test.h"
#include "world/star_system/planet_texture.h"

namespace {
bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
world::PlanetParams body(uint32_t seed, float r, float g, float b, bool moon) {
    world::PlanetParams p; p.seed = seed; p.moon = moon; p.color[0] = r; p.color[1] = g; p.color[2] = b;
    return p;
}
} // namespace

// The face orientation must be exactly GL's (spec table 3.21): a flipped sign mirrors a face and tears the texture at the cube edges.
TEST(planet_texture_cube_texel_dir_round_trips_through_the_gl_lookup_rule) {
    const int n = 16;
    for (int face = 0; face < 6; face++)
        for (int j = 0; j < n; j += 3)
            for (int i = 0; i < n; i += 3) {
                float d[3];
                world::cubeTexelDir(face, i, j, n, d);
                CHECK(near(d[0] * d[0] + d[1] * d[1] + d[2] * d[2], 1.0f, 1e-5f));
                int f = -1; float s = 0, t = 0;
                world::cubeLookup(d[0], d[1], d[2], f, s, t);
                CHECK_EQ(f, face);
                CHECK(near(s, ((float)i + 0.5f) / n, 1e-5f));
                CHECK(near(t, ((float)j + 0.5f) / n, 1e-5f));
            }
    // face centres point down the GL axes: +X -X +Y -Y +Z -Z
    const float axes[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int face = 0; face < 6; face++) {
        float d[3];
        world::cubeTexelDir(face, 0, 0, 1, d);   // a 1x1 face: its only texel is the centre
        for (int k = 0; k < 3; k++) CHECK(near(d[k], axes[face][k], 1e-6f));
    }
    // GL's own examples: +X face, s grows toward -Z, t grows toward -Y
    int f; float s, t;
    world::cubeLookup(1.0f, -0.5f, -0.5f, f, s, t);
    CHECK_EQ(f, 0); CHECK(near(s, 0.75f, 1e-6f)); CHECK(near(t, 0.75f, 1e-6f));
}

TEST(planet_texture_size_is_off_or_a_clamped_power_of_two) {
    CHECK_EQ(world::surfaceTextureSize(0), 0);
    CHECK_EQ(world::surfaceTextureSize(-64), 0);
    CHECK_EQ(world::surfaceTextureSize(1), 16);
    CHECK_EQ(world::surfaceTextureSize(64), 64);
    CHECK_EQ(world::surfaceTextureSize(100), 64);     // rounded down
    CHECK_EQ(world::surfaceTextureSize(128), 128);
    CHECK_EQ(world::surfaceTextureSize(512), 512);
    CHECK_EQ(world::surfaceTextureSize(8192), 1024);  // capped
    CHECK_EQ(world::surfaceTextureBytes(64), (size_t)64 * 64 * 3 * 6 * 4 / 3);
}

TEST(planet_texture_materials_follow_the_body) {
    std::set<int> seen;
    const float cols[7][3] = {{0.3f, 0.5f, 0.8f}, {0.8f, 0.4f, 0.2f}, {0.6f, 0.55f, 0.4f}, {0.2f, 0.6f, 0.3f}, {0.7f, 0.7f, 0.75f}, {0.8f, 0.6f, 0.4f}, {0.5f, 0.3f, 0.6f}};
    for (uint32_t s = 1; s < 200; s++) {
        uint32_t seed = world::mixSeed(1234U, s);
        auto moon = world::surfaceMaterial(body(seed, 0.5f, 0.5f, 0.5f, true));
        CHECK(moon == world::SurfaceMaterial::Regolith || moon == world::SurfaceMaterial::Ice);   // moons: no oceans, no biomes
        seen.insert((int)moon);
        for (auto& c : cols) {
            auto p = body(seed, c[0], c[1], c[2], false);
            auto m = world::surfaceMaterial(p);
            CHECK_EQ(m == world::SurfaceMaterial::Terran, world::planetHasOcean(p));         // terran <=> the mesh has an ocean
            if (m == world::SurfaceMaterial::Sand) CHECK(c[0] > c[2]);                     // sand only on warm bodies
            seen.insert((int)m);
            CHECK_EQ((int)world::surfaceMaterial(p), (int)m);                               // deterministic
        }
    }
    CHECK_EQ(seen.size(), (size_t)5);                                                       // every material occurs
    CHECK(std::string(world::materialName(world::SurfaceMaterial::Ice)) == "ice");
}

TEST(planet_texture_faces_are_deterministic_and_seeded) {
    auto p = body(77U, 0.6f, 0.55f, 0.4f, false);
    std::vector<uint8_t> a, b, c;
    world::buildSurfaceFace(p, 2, 32, a);
    world::buildSurfaceFace(p, 2, 32, b);
    CHECK_EQ(a.size(), (size_t)32 * 32 * 3);
    CHECK(a == b);                                     // same seed, same bytes (no clock, no global state)
    p.seed = 78U;
    world::buildSurfaceFace(p, 2, 32, c);
    CHECK(a != c);                                     // another body looks different
    std::set<uint8_t> values(a.begin(), a.end());
    CHECK(values.size() > 20);                         // a pattern, not a flat colour
}

// The texture replaces the vertex colours when it is on, so on average it must look like them: same palette, the detail
// pattern only varies it around the mean (a textured planet must not suddenly change colour when it comes into range).
TEST(planet_texture_average_matches_the_vertex_biome_colours) {
    const float cols[4][3] = {{0.3f, 0.5f, 0.8f}, {0.8f, 0.4f, 0.2f}, {0.7f, 0.7f, 0.75f}, {0.55f, 0.55f, 0.55f}};
    for (uint32_t seed : {3U, 17U, 4242U, 99991U})
        for (auto& c : cols)
            for (bool moon : {false, true}) {
                auto p = body(seed, c[0], c[1], c[2], moon);
                const bool ocean = world::planetHasOcean(p);
                const auto m = world::surfaceMaterial(p);
                double tex[3] = {0, 0, 0}, vert[3] = {0, 0, 0};
                const int n = 24;
                for (int face = 0; face < 6; face++)
                    for (int j = 0; j < n; j++)
                        for (int i = 0; i < n; i++) {
                            float d[3], tc[3], vc[3];
                            world::cubeTexelDir(face, i, j, n, d);
                            world::surfaceColor(p, ocean, m, d[0], d[1], d[2], tc);
                            world::biomeColor(p, ocean, world::terrainNoise(d[0], d[1], d[2], p.seed), std::fabs(d[1]), vc);
                            for (int k = 0; k < 3; k++) { tex[k] += tc[k]; vert[k] += vc[k]; CHECK(tc[k] >= 0.0f && tc[k] <= 1.0f); }
                        }
                const double texels = 6.0 * n * n;
                auto lum = [&](const double* v) { return (v[0] * 0.3 + v[1] * 0.59 + v[2] * 0.11) / texels; };
                if (m == world::SurfaceMaterial::Regolith) CHECK(std::fabs(lum(tex) - lum(vert)) < 0.05);           // greyed, same brightness
                else if (m == world::SurfaceMaterial::Ice) CHECK(lum(tex) >= lum(vert) - 0.02 && lum(tex) < lum(vert) + 0.2);   // paler, never darker
                else for (int k = 0; k < 3; k++) CHECK(std::fabs(tex[k] / texels - vert[k] / texels) < 0.07);      // terran/rock/sand: same palette
            }
}

TEST(planet_texture_halve_rgb_averages_2x2_blocks) {
    // 2x2 -> 1x1: the rounded mean of each channel
    std::vector<uint8_t> src = {0, 10, 255, 1, 20, 255, 2, 30, 0, 3, 40, 0}, dst;
    world::halveRGB(src, 2, dst);
    CHECK_EQ(dst.size(), (size_t)3);
    CHECK_EQ((int)dst[0], 2);      // (0+1+2+3+2)/4 = 2
    CHECK_EQ((int)dst[1], 25);     // (100+2)/4 = 25
    CHECK_EQ((int)dst[2], 128);    // (510+2)/4 = 128
    // 4x4 of four flat quadrants -> exactly those four colours
    std::vector<uint8_t> q(4 * 4 * 3);
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            for (int k = 0; k < 3; k++) q[(size_t)(y * 4 + x) * 3 + k] = (uint8_t)((y / 2) * 100 + (x / 2) * 50 + k);
    world::halveRGB(q, 4, dst);
    CHECK_EQ(dst.size(), (size_t)2 * 2 * 3);
    CHECK_EQ((int)dst[0], 0); CHECK_EQ((int)dst[3], 50); CHECK_EQ((int)dst[6], 100); CHECK_EQ((int)dst[9], 150); CHECK_EQ((int)dst[11], 152);
    // a full chain from 64 ends at 1x1
    std::vector<uint8_t> px;
    world::buildSurfaceFace(body(5U, 0.6f, 0.55f, 0.4f, true), 0, 64, px);
    int n = 64;
    while (n > 1) { world::halveRGB(px, n, dst); px.swap(dst); n /= 2; }
    CHECK_EQ(px.size(), (size_t)3);
}
