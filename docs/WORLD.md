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
