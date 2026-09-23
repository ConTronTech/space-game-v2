#pragma once
// Pure planet/moon surface textures (diffuse COLOUR only, no depth): no GL, no SDL, no engine types. Unit-tested in
// package/tests/test_planet_texture.cpp. See docs/WORLD.md "Surface textures".
//   * one cube map per body (six square RGB faces), baked on the CPU from the body's seed: sampled in GL with the mesh
//     vertex position as a 3D texture coordinate, so the icosphere needs no UVs, has no longitude seam and no pole pinch
//   * the colour scheme IS the vertex one (biomeColor from the same terrainNoise the geometry uses: coasts and snow caps
//     line up with the relief), just at texel resolution, times a per-material detail pattern (rock strata, sand ripples,
//     ice cracks, regolith craters/maria) whose mean is ~1, so a textured body averages the colour it had before
//   * "blending by a mask" (snow vs equator, sand vs rock by latitude/height) is done here per texel, once, at boot:
//     any number of layers for zero draw-time cost, instead of a fixed-function multitexture combiner
// Depth stays 100% in planet_mesh.h's geometry; nothing here moves a vertex.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "world/star_system/planet_mesh.h"

namespace world {

enum class SurfaceMaterial { Terran, Rock, Sand, Ice, Regolith };

inline const char* materialName(SurfaceMaterial m) {
    switch (m) {
        case SurfaceMaterial::Terran: return "terran";
        case SurfaceMaterial::Rock: return "rock";
        case SurfaceMaterial::Sand: return "sand";
        case SurfaceMaterial::Ice: return "ice";
        case SurfaceMaterial::Regolith: return "regolith";
    }
    return "rock";
}

// Material of a body, from what already decides its look: moons are regolith (1 in 4 icy), ocean worlds are terran,
// barren planets are sand when their colour is warm, ice (half of them, by seed) when it is cool, else rock.
inline SurfaceMaterial surfaceMaterial(const PlanetParams& p) {
    if (p.moon) return (hash32(p.seed ^ 0x1ce1ce1bU) % 4U) == 0U ? SurfaceMaterial::Ice : SurfaceMaterial::Regolith;
    if (planetHasOcean(p)) return SurfaceMaterial::Terran;
    const float* c = p.color;
    if (c[0] > c[2] + 0.15f) return SurfaceMaterial::Sand;
    if (c[2] >= c[0] && c[2] >= c[1] && (hash32(p.seed ^ 0x2c1b3c6dU) % 2U) == 0U) return SurfaceMaterial::Ice;
    return SurfaceMaterial::Rock;
}

// ---- cube map addressing (the OpenGL rule, GL 2.1 spec table 3.21) ----
// Face order is GL's: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z (GL_TEXTURE_CUBE_MAP_POSITIVE_X + face).
// Unit direction through the centre of texel (i, j) of `face` (i = column along s, j = row along t; row 0 is the first
// row glTexImage2D reads), for faces `n` texels wide.
inline void cubeTexelDir(int face, int i, int j, int n, float* out) {
    float a = 2.0f * ((float)i + 0.5f) / (float)n - 1.0f, b = 2.0f * ((float)j + 0.5f) / (float)n - 1.0f;
    float x = 0, y = 0, z = 0;
    switch (face) {
        case 0: x = 1;  y = -b; z = -a; break;
        case 1: x = -1; y = -b; z = a;  break;
        case 2: x = a;  y = 1;  z = b;  break;
        case 3: x = a;  y = -1; z = -b; break;
        case 4: x = a;  y = -b; z = 1;  break;
        default: x = -a; y = -b; z = -1; break;
    }
    float l = std::sqrt(x * x + y * y + z * z);
    out[0] = x / l; out[1] = y / l; out[2] = z / l;
}

// Which face and (s, t) in [0,1] GL samples for direction (x, y, z): the inverse of cubeTexelDir (used by the tests to pin
// the face orientation to the GL rule, since a wrong sign would mirror a face and tear the texture along cube edges).
inline void cubeLookup(float x, float y, float z, int& face, float& s, float& t) {
    float ax = std::fabs(x), ay = std::fabs(y), az = std::fabs(z), sc, tc, ma;
    if (ax >= ay && ax >= az) { ma = ax; if (x >= 0) { face = 0; sc = -z; tc = -y; } else { face = 1; sc = z; tc = -y; } }
    else if (ay >= az)        { ma = ay; if (y >= 0) { face = 2; sc = x; tc = z; }  else { face = 3; sc = x; tc = -z; } }
    else                      { ma = az; if (z >= 0) { face = 4; sc = x; tc = -y; } else { face = 5; sc = -x; tc = -y; } }
    s = (sc / ma + 1.0f) * 0.5f;
    t = (tc / ma + 1.0f) * 0.5f;
}

// Face size actually used for a requested size: 0 (or less) = texturing off; otherwise a power of two in [16, 1024],
// rounded DOWN (GL 2.1 allows non-power-of-two, but old Intel drivers mipmap them slowly or badly).
inline int surfaceTextureSize(int requested) {
    if (requested <= 0) return 0;
    int n = 16;
    while (n * 2 <= std::min(requested, 1024)) n *= 2;
    return n;
}

// Bytes of one body's cube map on the GPU (RGB, full mip chain ~ 4/3 of level 0).
inline size_t surfaceTextureBytes(int size) { return (size_t)size * (size_t)size * 3U * 6U * 4U / 3U; }

// ---- the colour of one texel ----
// Diffuse colour at unit direction (x, y, z), components in [0,1]. `ocean` = planetHasOcean(p), `m` = surfaceMaterial(p)
// (passed in so a face loop does not recompute them per texel).
inline void surfaceColor(const PlanetParams& p, bool ocean, SurfaceMaterial m, float x, float y, float z, float* out) {
    using namespace detail;
    const float h = terrainNoise(x, y, z, p.seed);
    float col[3];
    biomeColor(p, ocean, h, std::fabs(y), col);                    // the same palette the mesh vertices get
    const bool water = ocean && h < kSeaLevel;
    const uint32_t s2 = p.seed + 0x2545f491U;
    const float g = fbm(x * 24.0f, y * 24.0f, z * 24.0f, s2, 3);   // fine grain, mean ~0.5
    float k = 1.0f;
    switch (m) {
        case SurfaceMaterial::Terran:
            k = water ? 0.94f + 0.12f * g : 0.82f + 0.36f * g;
            break;
        case SurfaceMaterial::Rock: {                              // warped horizontal strata + grain
            float w = fbm(x * 3.0f, y * 3.0f, z * 3.0f, s2 + 11U, 3);
            float band = 0.5f + 0.5f * std::sin(y * 18.0f + w * 6.0f);
            k = 0.7f + 0.3f * g + 0.3f * band;
            break;
        }
        case SurfaceMaterial::Sand: {                              // wind ripples along latitude, bent by noise
            float w = fbm(x * 4.0f, y * 4.0f, z * 4.0f, s2 + 23U, 2);
            float r = 0.5f + 0.5f * std::sin(y * 60.0f + w * 20.0f);
            k = 0.85f + 0.15f * r + 0.15f * g;
            const float warm[3] = {0.86f, 0.74f, 0.52f};
            mixc(col, warm, 0.2f, col);
            break;
        }
        case SurfaceMaterial::Ice: {                               // pale blue-white sheet with dark ridged cracks
            const float ice[3] = {0.82f, 0.9f, 0.98f};
            mixc(col, ice, 0.45f, col);
            float c = std::fabs(fbm(x * 10.0f, y * 10.0f, z * 10.0f, s2 + 31U, 4) - 0.5f) * 2.0f;
            float crack = 1.0f - smooth(0.0f, 0.06f, c);
            k = (0.96f + 0.1f * g) * (1.0f - 0.35f * crack);
            break;
        }
        case SurfaceMaterial::Regolith: {                          // grey dust, darker maria, pits with bright rims
            float lum = col[0] * 0.3f + col[1] * 0.59f + col[2] * 0.11f;
            const float grey[3] = {lum, lum, lum};
            mixc(col, grey, 0.5f, col);
            float v = valueNoise(x * 14.0f, y * 14.0f, z * 14.0f, s2 + 41U);
            float pit = smooth(0.72f, 0.8f, v), rim = smooth(0.62f, 0.7f, v) - pit;
            float mare = fbm(x * 1.5f, y * 1.5f, z * 1.5f, s2 + 53U, 2);
            k = (0.85f + 0.3f * g) * (1.0f - 0.3f * pit + 0.15f * rim) * (0.85f + 0.3f * smooth(0.35f, 0.6f, mare));
            break;
        }
    }
    for (int i = 0; i < 3; i++) out[i] = std::clamp(col[i] * k, 0.0f, 1.0f);
}

// ---- baking ----
// One face (`n` x `n` RGB bytes, tightly packed, row 0 first) of the body's cube map, into `out` (resized).
inline void buildSurfaceFace(const PlanetParams& p, int face, int n, std::vector<uint8_t>& out) {
    const bool ocean = planetHasOcean(p);
    const SurfaceMaterial m = surfaceMaterial(p);
    out.resize((size_t)n * (size_t)n * 3U);
    float d[3], c[3];
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) {
            cubeTexelDir(face, i, j, n, d);
            surfaceColor(p, ocean, m, d[0], d[1], d[2], c);
            uint8_t* o = &out[((size_t)j * (size_t)n + (size_t)i) * 3U];
            for (int k = 0; k < 3; k++) o[k] = (uint8_t)std::lround(c[k] * 255.0f);
        }
}

// Next mip level: `src` is n x n RGB (n >= 2, even), `dst` becomes n/2 x n/2, each texel the average of a 2x2 block (rounded).
inline void halveRGB(const std::vector<uint8_t>& src, int n, std::vector<uint8_t>& dst) {
    int h = n / 2;
    dst.resize((size_t)h * (size_t)h * 3U);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < h; i++)
            for (int k = 0; k < 3; k++) {
                auto at = [&](int x, int y) { return (int)src[((size_t)y * (size_t)n + (size_t)x) * 3U + (size_t)k]; };
                int sum = at(2 * i, 2 * j) + at(2 * i + 1, 2 * j) + at(2 * i, 2 * j + 1) + at(2 * i + 1, 2 * j + 1);
                dst[((size_t)j * (size_t)h + (size_t)i) * 3U + (size_t)k] = (uint8_t)((sum + 2) / 4);
            }
}

} // namespace world
