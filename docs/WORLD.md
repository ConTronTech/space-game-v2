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
| `starfield.warp_ref_speed` | 2000 | m/s for full-length streaks |

## `world/skybox` (pass `skybox`, order -100: behind everything)
Six textured quads around the camera (view translation removed, depth test/writes off, lighting off; state restored afterwards).
It scans `skybox.dir` (default `assets/skybox/bkg`) for `<color>/<set>/` folders holding all six faces (`front back left right top bot`, also `_ft _bk _lf _rt _up _dn`, `.png/.jpg/.bmp`).
`skybox.set` (default `dark/set1`) picks one; if missing the first set (alphabetical) is used and a warning says so. No sets or an unreadable face: one warning and no skybox.

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
| `skybox.set` | dark/set1 | `color/set` |
| `skybox.max_size` | 1024 | longest side, pixels |
| `skybox.dir` | assets/skybox/bkg | scan folder |
| `skybox.cache_dir` | cache | |

## Adding a skybox set
Create `assets/skybox/bkg/<color>/<set>/` with the six face images (any resolution; big ones are downscaled once and cached), add a `skybox.json` only if a face looks flipped or rotated, and set `skybox.set` to `<color>/<set>`.

## Code map
Pure rules (no SDL/GL): `world/skybox/skybox_rules.h` (capped size, face names, UV transform, set choice, cache path/freshness, box downscale) and `world/starfield/starfield_rules.h` (streak length), tested in `package/tests/test_world.cpp`.
The demo rocks stay in `ship/ship_core` until the real world replaces them.

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
