# World backdrop: starfield and skybox

Both modules draw through `core::RenderEngine` passes and use only OpenGL 2.1 fixed function (built for the low-end target laptop, see `docs/VISION.md`).

## `world/starfield` (pass `starfield`, order 0)
Points on a far sphere (radius 5000) that follow the camera (no parallax). Positions come from a seeded `std::mt19937`, so the field is the same every run.
Uses vertex arrays; nothing is allocated per frame. GL state (depth test, point/line size, colour) is saved and restored.

**Warp streaks:** while `ship::IShip::status().warping` is true each star becomes a line pointing away from the direction of travel (opposite `velocity()`),
bright at the star and dim at the tail, with length `starfield.warp_streak_length * min(1, speed / starfield.warp_ref_speed)`. No `IShip`, or not warping: plain points.

| Tunable | Default | |
|---|---|---|
| `starfield.enabled` | true | draw the stars |
| `starfield.count` | 2500 | number of stars |
| `starfield.seed` | 1234 | position seed |
| `starfield.point_size` | 1.5 | pixels |
| `starfield.brightness` | 1.0 | multiplier |
| `starfield.warp_streak_length` | 300 | world units at full warp speed; 0 = no streaks |
| `starfield.warp_ref_speed` | 5000 | m/s for full-length streaks |

## `world/skybox` (pass `skybox`, order -100: behind everything)
Six textured quads around the camera (view translation removed, depth test/writes off, lighting off; state restored afterwards).
It scans `skybox.dir` (default `assets/skybox/bkg`) for `<color>/<set>/` folders holding all six faces (`front back left right top bot`, also `_ft _bk _lf _rt _up _dn`, `.png/.jpg/.bmp`).
`skybox.set` picks one (`color/set`); if missing the first set (alphabetical) is used and a warning says so. No sets or an unreadable face: one warning and no skybox.

**Sky matches the star (default).** `skybox.set` defaults to `auto`: the set whose sampled colour is closest to the seeded sun's colour (`IStarSystem::bodies()[0].color`, read once at init; `world/star_system` is an *optional* dependency) is used, so a blue star gets a blue sky.
- Each set's colour is the average RGB of its **front** face (decoded once and box-downscaled to 1x1, then cached as `cache/skybox/<color>_<set>_avg.txt`; fresh while not older than the face). The folder name is not used. First start: ~0.6 s extra for 18 sets on the desktop, later starts read 18 tiny text files (nothing per frame).
- Compared as chromaticity (r,g,b divided by r+g+b), because the skies are dark and the star is bright. The star's faint tint is boosted away from grey by `skybox.star_tint_gain` (3) first; then plain Euclidean distance, nearest wins (`world::chroma`, `boostTint`, `nearestColor`, `resolveWantedSet` in `skybox_rules.h`, unit-tested).
- Measured with the shipped sets: pale blue -> `lightblue/set1`, orange -> `orange/set2`, red-orange -> `red/set2`, near-white -> `dark/set3`, warm white -> `green/set3` (its average is a warm yellow-brown). Log line: `star colour (r,g,b) -> chosen set 'X/Y' (distance D of N sets)`.
- **An explicit choice always wins:** any `skybox.set` value other than `"DEFAULT"`/missing/`auto` is used exactly as written (the config returns the code default `auto` only for a missing key or `"DEFAULT"`). `skybox.match_star_color` false, no star system (`--disable=world/star_system`, `star_system.enabled` false) or no set that could be sampled: `dark/set1`, the old fixed default.
- A matched set can have bigger faces than `dark/set1` (e.g. `red/set2` is 2048 px = 72 MB at cap 2048); `skybox.max_size` from the quality preset still caps it (Low = 512).

**Per-set orientation:** optional `skybox.json` in the set folder, e.g. `{ "top": {"flip_v": true}, "bottom": {"rotate": 90} }` (faces `front back left right top bottom`;
keys `flip_u`, `flip_v`, `rotate` 0/90/180/270; `flipU`/`flipV` also accepted). Applied as in the old game.

**Implicit flip on top/bottom:** the shipped sets' `skybox.json` files follow the OLD game's convention, and V2 renders the top and bottom faces vertically inverted relative to it.
So V2 applies an implicit `flip_v` on those two faces: effective flipV = implicit XOR the json `flip_v` (`world::effectiveFaceUV`, unit-tested). A set without json (e.g. `dark/set1`) gets the implicit flip alone;
a set whose json says `"top": {"flip_v": true}` cancels it. Side faces are unaffected, as are `flip_u` and `rotate`. So a json you write for a new set is relative to the old convention: only add a `flip_v` on top/bottom
if the face looks wrong after the implicit flip. (Cause not found: the quad corners and UV order match the old `skybox.h`; the fix was measured, see below.)

### Checking the seams: `package/tools/skybox_seams.py`
`package/tools/skybox_seams.py [--sets dark/set1,red/set2] [--threshold 1.8]` points the ship at the top, bottom and two side seams of each set (via generated saved games), takes real-game screenshots and compares the pixels either side of each seam
with the normal pixel-to-pixel change elsewhere. Ratio 1.0 = seamless; above the threshold it exits 1. Needs the built game, numpy, Pillow and a display; works on symlinked copies (never touches `assets/`) and temporarily replaces `config/game.json`. Without `--sets` it checks every set (several minutes).
The ratio is a heuristic: very busy content at the seam can push a correct seam a little over 1.5.

### Texture budget (low-end GPUs share system memory)
Faces load **one at a time**: decode, box-filter (area average) down to at most `skybox.max_size` (default 1024, and never above `GL_MAX_TEXTURE_SIZE`) on the longest side, free the big image, then upload RGB with `GL_LINEAR` and clamp-to-edge.
Six 1024 faces = 18 MB; at `max_size` 512 it is 4.5 MB. The startup log line shows the set, face size, total texture MB, load time and whether the cache was used.

### Downscale cache
A downscaled face is saved to `cache/skybox/<color>_<set>_<maxsize>/<face>.png` (git-ignored, `skybox.cache_dir`, empty = off). On the next start the cached file is used when it exists, is not older than the source and decodes;
otherwise the original is loaded again. Faces already at or below the cap are not cached (nothing to save). Delete `cache/` any time.

| Tunable | Default | |
|---|---|---|
| `skybox.enabled` | true | |
| `skybox.set` | auto | `color/set`, or `auto` = match the star (see above) |
| `skybox.match_star_color` | true | off = `dark/set1` when `skybox.set` is auto |
| `skybox.star_tint_gain` | 3 | star tint boost before matching (1 = raw) |
| `skybox.max_size` | 1024 | longest side, pixels |
| `skybox.dir` | assets/skybox/bkg | scan folder |
| `skybox.cache_dir` | cache | |

## Adding a skybox set
Create `assets/skybox/bkg/<color>/<set>/` with the six face images (any resolution; big ones are downscaled once and cached), add a `skybox.json` only if a face looks flipped or rotated, and set `skybox.set` to `<color>/<set>`.

## Code map
Pure rules (no SDL/GL): `world/skybox/skybox_rules.h` (capped size, face names, UV transform, set choice, star colour matching, cache path/freshness, box downscale) and `world/starfield/starfield_rules.h` (streak length), tested in `package/tests/test_world.cpp`.
The 150 demo rocks of `ship/ship_core` are now OFF by default (`flight.demo_rocks`, see below); the real asteroids are `world/asteroids`.

## `world/star_system` (pass `star_system`, order 50: after starfield/skybox, before the demo rocks)
A seeded, deterministic star system: one sun, `planets.count` planets (2-10, default 6), 0-3 moons each. Provides `world::IStarSystem` (`star_system_api.h`): `sunPosition()`, `bodies()` (id, name, kind sun/planet/moon,
position in **double**, radius, parent id, orbit radius, period, colour), `positionAt(id)`, `simTime()`, `seed()`. Radar, navigation and the collision wiring (3.4) can consume it; nothing here touches physics or ship behaviour yet.

**Generation** (`generateSystem`, pure, from a `std::mt19937` with platform-independent number conversion): behaviour ported from the old game: sun radius 800-2000 in one of five star colours; the first planet ~8 sun radii + 20-40k out, then 30-80k
(+10 planet radii) further for each next one, so the outer edge is 100k-350k units; inner planets small (150-450), outer big (400-1200); seven planet colours; moons 40-(30% of the planet) big, a few thousand units out.
Same seed = identical system (unit-tested). The sun is placed `world.sun_distance` (12000) from the origin in a seeded direction, so the ship's default spawn is clear of every body: nearest body is the sun, ~10,000 units from its surface, and every planet stays more than 13,000 units from the origin for ever (smallest orbit 26,400 minus the sun offset 12,000, minus a planet radius).

**Orbits:** circular, per-body inclination (planets +-10 deg, moons +-15 deg), analytic: `position = parent + r*(cos a, sin a * sin tilt, sin a * cos tilt)`, `a = phase + 2 pi t / period`. No integration, so no drift however long the game runs.
Sim time advances only in `onFixedUpdate` (so it stops while paused) at `world.time_scale` (1 = real seconds; the periods are hours, as in the old game, so try 100 to watch them move). It is saved (`world/star_system`: seed + time) and restored on load; a save with another seed regenerates the system.

**Camera-relative rendering (precision):** positions are doubles. Each frame the camera position (from the view matrix) is subtracted in double, and only the small difference goes to float. Bodies farther than 75% of the far plane are pulled in along the line of sight to that distance
and scaled by the same factor, so a planet 60,000 units away is still a dot with the correct angular size (`projectBody`, unit-tested). Those backdrop bodies are painted far-to-near with depth testing off; nearer bodies are real depth-tested geometry. No world re-origin is done and no other module changes.

**Drawing:** the sun is an emissive (unlit) sphere plus an additive camera-facing glow; planets and moons are lit, colour-tinted spheres with one `GL_LIGHT0` per body pointing at the sun (a directional light, `w = 0`).
The unit sphere is built once (vertex + index arrays, no per-frame allocation). All GL state is saved and restored. Placeholder meshes: real LOD meshes come with 3.3.

| Tunable | Default | |
|---|---|---|
| `star_system.enabled` | true | generate and draw |
| `world.seed` | 1234 | system seed |
| `planets.count` | 6 | 2-10 |
| `world.sun_distance` | 12000 | sun from the spawn point, units |
| `world.time_scale` | 1.0 | orbit speed multiplier |
| `world.sphere_detail` | 1 | 0 = 12x8 segments, 1 = 16x12 |

Logging at `engine.log_level: debug` lists every body with its orbit radius and period.

**Adding a body kind** (e.g. asteroid belt, station): add a value to `world::BodyKind` in `star_system_api.h`, create the bodies in `generateSystem` (parent index lower than the child's, `orbitRadius`/`period`/`phase`/`tilt` describe the orbit),
and handle the new kind in `drawBody` in `star_system.cpp`. Consumers that switch on `kind` need a case for it.

### Collisions (3.4)
With `core/physics_world` loaded, every body is registered as a static sphere of the drawn radius, kind `sun` / `planet` / `moon` (no physics module = no bodies, no crash). Each fixed step, after the orbits advance, `setBody(id, pos, vel)` moves the sphere
(velocity = position change / dt), so the physics sweep sees the moving planet and the reported closing speed is relative to it. Loading a save teleports them instead. The physics world steps after us (priority 10).
Float precision: the physics API is `float`. At 350,000 units one float step is 0.031 units, fine for spheres of radius 2.5+; the swept test subtracts positions of magnitude 3.5e5 (squares ~1e11), giving a contact-distance error of a few units at that range.
That is acceptable for planets (radius 200-1200) but would matter for small objects out there; a later fix is double-precision positions in the physics world or rebasing bodies relative to the ship. Nothing was changed in `core/physics_world`.
Ship reaction: see docs/SHIP.md (planet/moon tiered damage + bounce, sun lethal, `ship.sun_kills`).

## Planet meshes and LOD (3.3)
Planets and moons are terrain meshes instead of plain spheres (the sun and bodies smaller than ~2.5 px on screen keep a sphere; dots use a 96-triangle one). All of it is pure code in `world/star_system/planet_mesh.h` (no GL), tested in `package/tests/test_planet_mesh.cpp`.

**Mesh:** an icosphere of subdivision level 0..5 (`world::kMaxMeshLevel`; 20, 80, 320, 1,280, 5,120, 20,480 triangles; `10*4^L+2` shared vertices, `uint16` indices), built for radius 1 and drawn scaled. Heights come from our own seeded value-noise FBM
(two blended fields, same numbers on every platform; seed = `mixSeed(world.seed, body id)`), displaced by `world.terrain_height` (default 0.03 = at most 3% of the radius; oceans are flat at sea level; `world::surfaceRadiusFactor(params, dir)` gives that drawn radius at any unit direction, used by `world/stations` to sit surface stations on the ground). Colours are biomes from height and latitude
(deep water, sand, grass/lowland, rock, snow at the poles and peaks), derived from the body colour; two thirds of planets have oceans, moons never. Normals are area-weighted from the displaced faces (smooth, no seams).
Vertex format: interleaved position/normal/colour (36 bytes per vertex), drawn with client vertex arrays (`glVertexPointer/NormalPointer/ColorPointer` + `glDrawElements`; VBOs were not needed at these sizes and would add GL 1.5 entry-point handling), lit by the sun's `GL_LIGHT0` with `GL_COLOR_MATERIAL`.

**LOD:** the level follows the projected radius in pixels (`pixelRadius`): an icosphere edge spans about `1.1 * px / 2^level` pixels and the target is `world.planet_lod_edge_px` (12). `chooseLod` has hysteresis (0.3 of a level) so a body at a boundary does not flicker.
`world.planet_max_lod` (default **3**, 1,280 triangles) caps it: level 4 costs 4x the triangles and 4x the build time for a smaller visual gain at 1280x720; raise it for a beefier machine. The Ultra preset uses level 5 (3.3c; build cost and why not 6 in docs/QUALITY.md). `world.planet_triangle_budget` (default 20,000) caps the planet triangles per frame:
`applyTriangleBudget` lowers the level of the least important body (most triangles per pixel of importance) until it fits. Typical frames draw 1,600-3,400 triangles (a few meshes plus dots).

**Lazy building:** nothing is built at startup. A mesh is built the first time its level is wanted, **at most one per frame** (the biggest body on screen first); until it exists the nearest already-built level (or the sphere) is drawn. Built meshes are cached per (body, level) and freed after
`world.planet_mesh_cache_seconds` (30) unused. Build times at `engine.log_level: debug` (this dev machine, -O2): level 0 0.01 ms, level 3 0.3 ms, level 4 1.1 ms (2,562 vertices). Estimate for the target laptop 5-10x slower: level 4 about 6-11 ms once per planet, level 3 about 2-3 ms:
fits in the one-build-per-frame rule. Memory: level 3 = 31 KB, level 4 = 121 KB per mesh; a handful of planets is well under 1 MB.

**Physics:** the collision sphere stays the base radius. With the default relief the largest mismatch is 3% of the radius (up to about 34 units on the biggest planet, radius 1,134): mountains poke that far out of the sphere and lowlands sit that far inside it.

| Tunable | Default | |
|---|---|---|
| `planet_mesh.enabled` | true | false = the old plain spheres |
| `world.terrain_height` | 0.03 | relief as a fraction of the radius |
| `world.planet_max_lod` | 3 | highest level 0-5 |
| `world.planet_triangle_budget` | 20000 | planet triangles per frame |
| `world.planet_lod_edge_px` | 12 | target edge size in pixels |
| `world.planet_mesh_cache_seconds` | 30 | free unused levels after this |

At debug log level the module prints each build (time, memory) and, every 2 s, the meshes drawn, triangles this frame, cache size and the slowest build.

## Surface textures (diffuse colour only)
Depth stays **100% in the mesh geometry** above; textures only add colour/material variety. No normal or bump mapping (no shaders, and dot3-combiner tricks fight the old-GPU target).
Pure code in `world/star_system/planet_texture.h`, tested in `package/tests/test_planet_texture.cpp`.

**Technique: one cube map per body, no UVs.** Each planet/moon gets a `GL_TEXTURE_CUBE_MAP` (GL 1.3 core: every GL 2.1 driver has it, including Ironlake/GMA; only constants, no new entry points). The draw passes the mesh's own vertex positions as a 3-component texture coordinate (`glTexCoordPointer(3, ..., kStride, positions)`): a cube map is looked up by *direction*, and the displaced position points the same way as the unit direction it was built from. So the mesh format is unchanged (no UVs), and there is **no longitude seam and no pole pinch** (an equirectangular UV wrap on an icosphere would need duplicated seam vertices and still smear at the poles). The fallback spheres (not-yet-built mesh, far dots) are textured the same way, so nothing changes colour when a mesh level pops in. `GL_MODULATE` with white vertex colour: the texel is lit exactly like the old vertex colour was.

**What is in a texel.** `surfaceColor` = the *same* `biomeColor` palette the vertices use, from the *same* `terrainNoise` the geometry is displaced by (coastlines and snow caps line up with the relief, now at texel resolution instead of one colour per vertex), times a per-material detail pattern whose mean is ~1:
| Material (`surfaceMaterial`) | Who | Pattern |
|---|---|---|
| terran | planets with an ocean | fine grain on land, subtle ripple on water |
| rock | barren, neither warm nor (by seed) icy | warped horizontal strata + grain |
| sand | barren, warm colour (red > blue + 0.15) | wind ripples along latitude, bent by noise; warm tint |
| ice | barren cool-coloured planets (half, by seed); 1 in 4 moons | blue-white sheet with dark ridged cracks |
| regolith | the other moons | desaturated, darker maria, pits with bright rims |
The unit tests pin that terran/rock/sand average within 0.07 per channel of the vertex palette, regolith keeps its brightness, and ice only gets paler.

**Multitexture survey (the open item from docs/NEXT_UP.md).** The codebase used no `glActiveTexture`, no `GL_COMBINE` and no second texture unit anywhere, so any two-unit blend (`ARB_multitexture` + `ARB_texture_env_combine` `GL_INTERPOLATE` with a mask in a texture's alpha) would be unproven on the Ironlake target. It is also not needed for what the plan wanted it for: "snow cap vs equator band", "rock vs sand by latitude" are **baked per texel on the CPU** (`biomeColor` already blends water/sand/grass/rock/snow by height and latitude, and the material pass layers on top), which gives any number of masked layers for zero draw-time cost and one texture unit. Where hardware multitexture *would* still add something: a small tiling detail texture on unit 1 (`GL_COMBINE` `GL_ADD_SIGNED` or modulate-2x) for close-range grain beyond the cube-map resolution. Not done; it needs the entry points looked up (as `render_engine` does for FBOs) and a live test on the laptop first.

**Baked at boot, never mid-flight.** `generate()` bakes every body's six faces and uploads them (level 0 plus a CPU box-filtered mip chain down to 1x1, `GL_LINEAR_MIPMAP_LINEAR`, clamp-to-edge; same upload calls as `world/skybox`). The six faces of a body are baked on up to six `std::thread`s (pure, independent: same bytes either way; serial fallback). Between bodies it calls `Engine::bootStep`, which shows `LOADING world/star_system: planet textures 3/12` and moves the bar within the module's slice (`engine::BootStep`, `core/boot_screen`), and pumps one boot frame so the window stays responsive. Loading a save with a different seed re-bakes mid-game (a one-off wait at load time, logged). Startup logs `baked N surface textures (S px cube faces, X MB with mips) in T ms during boot`.

**Cost** (this dev machine, -O2, single thread per body): 64 px 10 ms, 128 px 42 ms, 256 px 168 ms, 512 px ~670 ms per body; divide by up to 6 with the face threads. A typical system has ~10-15 bodies. GPU memory per body (RGB + mips): 64 px 96 KB, 128 px 384 KB, 256 px 1.5 MB, 512 px 6 MB.

| Tunable | Default | |
|---|---|---|
| `world.planet_texture_size` | 128 | cube-map face size, a power of two 16-1024 (rounded down; capped by `GL_MAX_CUBE_MAP_TEXTURE_SIZE`); 0 = off (vertex colours, the old look). Presets: Low 64, Medium 128, High 256, Ultra 512 |

## Planet atmospheres (3.3b)
`world/atmosphere` (passes `world/atmosphere` order 120 and `world/atmosphere_tint` order 150) draws a rim-glow shell around each nearby planet and a faint
screen tint inside it. It reads `IStarSystem` only; visual only (no physics). See docs/ATMOSPHERE.md.

## `world/asteroids` (3.6, pass `asteroids`, order 60: after the star system, before the demo rocks)
Belts and clusters of **static** asteroids (a belt is a shape, not a simulation: they do not orbit, and cluster asteroids stay where their planet was at the start of the run). Provides `world::IAsteroids` (`asteroids_api.h`): `count()`, `position(i)`, `radius(i)`, `ore(i)`, `nearest(p, n, out)` for the radar / mining (Phase 4.3);
indices are stable for the whole run. Without `world/star_system` nothing is generated.

**Generation** (`asteroid_rules.h`, pure, seeded from `world.seed`; `generateField`): a belt is a band around the sun in the XZ plane between two planet orbits (at most `beltMaxWidth` = 3,000 units wide, about 1,000 thick, patchy: a noise field thins some parts out);
a cluster is a flattened shell around a planet (outside its moons) or a moon. Sizes follow the old game (belts: 50% radius 1-5, 25% 5-15, 15% 15-35, 7% 35-60, 3% 60-120; clusters mostly 0.5-4); ore ids and rarity weights come from `data/ores.json` through `core::IData` (else "rock");
each has a spin axis and rate and one of 4 shared mesh variants. Stored as struct-of-arrays (about 60 bytes each). Physics radius = radius x 0.9, so the ship damage tiers (`asteroid` radius < 5 small, < 30 medium, else big) apply to the physics radius.
Default field (1 belt of 1,500 + 4 clusters of 60 = 1,740 asteroids, 12 shared meshes): generated in 0.8 ms on the dev machine (about 5-10 ms estimated on the laptop), 117 KB.

**Drawing:** camera-relative (double subtract, then float), distance-culled (`asteroids.draw_distance`) and view-cone-culled; meshes are lumpy icospheres of 20 / 80 / 320 triangles built once (4 variants x 3 levels) and drawn with client vertex arrays,
one push/translate/rotate(spin)/scale per asteroid. The level follows the on-screen size (`asteroids.lod_edge_px`); at most `asteroids.max_drawn` meshes are drawn, nearest first, within `asteroids.triangle_budget` triangles (the farthest are lowered first, then turned to points);
everything smaller than 2 px, and everything past the cap, is one batch of GL points. Lit by `GL_LIGHT0` toward the sun (fixed light without a star system); all GL state is restored. Nothing is allocated per frame.

**Physics pool:** static `asteroid` bodies exist only for asteroids within `asteroids.physics_radius` (1,500) of the ship: added at that radius (nearest first, at most 32 per fixed step), removed at 1.2x the radius, never more than `asteroids.max_bodies` (300) alive.
A warp crash into a registered asteroid reports the full closing speed (checked: 2,000 m/s, fatal); a 100 m/s hit on a radius-9 asteroid does 10 damage (medium tier) and bounces.

| Tunable | Default | |
|---|---|---|
| `asteroids.enabled` | true | |
| `asteroids.belt_count` / `belt_asteroids` | 1 / 1500 | |
| `asteroids.cluster_count` / `cluster_asteroids` | 4 / 60 | |
| `asteroids.draw_distance` | 6000 | units |
| `asteroids.max_drawn` | 400 | meshes per frame |
| `asteroids.triangle_budget` | 15000 | per frame |
| `asteroids.lod_edge_px` | 10 | |
| `asteroids.physics_radius` / `max_bodies` | 1500 / 300 | |
| `flight.demo_rocks` | **false** | the old 150 demo rocks of `ship/ship_core` (drawing and physics); saved games that referred to "the first rock" at (-23.2, -161.3, -141.6) need `flight.demo_rocks: true` |

Laptop notes: a stress run with 21,500 asteroids drew 25 meshes and about 4,000 points with 300 physics bodies at 1,700 fps on the dev machine; the cost scales with the asteroids inside the draw distance, not with the field size (the culling loop is one pass over the field per frame, fine into the tens of thousands).
The radar does not show asteroids yet: it needs a layer that calls `IAsteroids::nearest(shipPos, N, out)` (see docs/COCKPIT.md).

### Ore zones (6.3, `data/ore_zones.json`)
Which ore an asteroid holds depends on how far its belt / cluster is from the sun: `data/ore_zones.json` lists distance bands, each multiplying the `data/ores.json` rarity per ore (then renormalized; `world::zoneOreTable` in `asteroid_rules.h`, pure, unit-tested). A **belt** uses its orbit-gap midpoint (halfway between the two planet orbits it sits between), a **cluster** its planet/moon's distance from the sun at generation. Placement, sizes and the RNG sequence are unchanged: only the ore pick's weights differ.
Boundaries are grounded in seed 1234's planet orbits (46,493 / 90,501 / 140,369 / 218,380 / 280,726 / 352,200; the belt's gap midpoint is 179,375):

| Zone | Distance | Holds (seed 1234) | Multipliers (missing = 1) | Result (share of the table) |
|---|---|---|---|---|
| inner | 0 - 110,000 | planets 1-2 (start area) | iron 1.6, copper 1.4, gold 0.8, cobalt 0.8, uranium 0.4, platinum 0.35, crystal 0.35 | iron ~48%, platinum ~0.8%, crystal ~0.5% |
| mid | 110,000 - 200,000 | planet 3, the belt | uranium 1.2, platinum 1.3, crystal 1.2 | iron 35.6%, platinum 3.5% (close to the old global table) |
| outer | 200,000 - 320,000 | planets 4-5 | iron 0.7, copper 0.8, gold/cobalt 1.3, uranium 2.0, platinum/crystal 2.5 | iron 25.1%, platinum 6.7% |
| deep | 320,000 + | planet 6 and beyond | iron 0.5, copper 0.6, titanium 1.2, gold/cobalt 1.5, uranium 2.5, platinum 3.5, crystal 4.0 | iron 17.5%, platinum 9.2%, crystal 7.0% |

Reasoning: the start is near the inner planets, so close in it is the common crafting metals; the belt stays roughly the old balance; the payoff for flying out is that platinum is ~3.5x (outer ~2.5x) as likely at the edge as near the start, and ~12x more than in the inner zone. **No multiplier is 0**: every ore stays findable in every zone (smallest share: inner crystal/platinum ~0.5-0.8%), to avoid a "can't find X anywhere near me" lockout. A distance in no zone = multiplier 1 for everything; file missing/empty = exactly the old single global table (unit test: identical ore for every asteroid).
Check: `--ore-zone-dump` logs each planet orbit, then per belt / cluster its distance, zone, expected % and actual count per ore. Seed 1234 (default quality): belt (mid, 4,000) iron 35.4% / platinum 2.9% / crystal 2.3%; outer cluster (150) iron 22.0% / platinum 6.0% / uranium 10.7%; deep cluster (150) iron 12.0% / uranium 18.0% / platinum 7.3%. With the file emptied the same run gives the belt iron 36.0% / platinum 2.4% and the deep cluster iron 32.7% / platinum 0.7%. (This seed puts no cluster in the inner zone; the tests cover it.)

## Stations
`world/stations` adds 1-3 stations (orbital or planetary, placeholder cube + cylinder models) and `ship/docking` the dock key: see docs/STATIONS.md.

## Asteroid health (4.2a)
Asteroids have hit points `asteroids.hp_scale * radius^2` (default 1.25: a radius-9 rock has about 100 HP: ten blaster bolts of 10, or 3-4 s of mining beam at 30 per second; a radius-60 rock about 4,500). `world::IAsteroids` gained NON-PURE extensions with safe defaults:
`alive(i)` and `damage(i, amount, hitPos)` (true when that call destroyed it). A destroyed asteroid vanishes from drawing, from the physics pool (its body is removed) and from `nearest()` (so from the radar), emits `world::AsteroidDestroyed{id, position, radius, ore}` (what mining, 4.3, will turn into ore drops)
and a debris + spark burst through `fx::SpawnParticles`. Indices stay valid (`position(i)` and `radius(i)` still answer for a destroyed one). **Destroyed state is not saved yet**: the field regenerates on load; 4.3 will save it. Weapons: docs/COMBAT.md.

## `world/anomalies` (6.1)
Seeded points of interest (deep space, near planets/moons, in the belt) from the same `world.seed`, found with the Anomaly Scanner perk: see docs/ANOMALIES.md.
