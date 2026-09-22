# Planet atmospheres (3.3b, `world/atmosphere`)

Visual only. A thin glowing shell around every planet near the camera, bright at the limb and clear when you look straight down at the
surface, plus a faint colour wash while the camera is inside a shell. Rules: `package/modules/world/atmosphere/atmosphere_rules.h`
(unit tests: `package/tests/test_atmosphere.cpp`). Needs `core/render_engine`; reads `world/star_system` (read-only, and it does nothing if that module is off).

**No gameplay effect.** Flying into a shell changes nothing about ship physics, drag, heat, damage or collision: the module draws only,
and does not touch ship_core, gravity or physics_world. `--disable=world/atmosphere` removes it cleanly.

## Why the old game's version looked "iffy"
The old game (`space-game/include/world/starsystem.h`, `drawAtmosphere`) drew 3 nested `gluSphere` shells with ONE flat alpha per shell, set
only by the camera distance. So the whole disc had the same haze (no bright limb, no clear centre), the 3 shells made bands, and inside the
atmosphere it turned depth testing off and tinted the screen, which let things behind other geometry bleed through.

## Technique (no shaders: OpenGL 2.1 fixed function)
One low-poly sphere shell per planet (radius 1.06 x the planet), drawn with client vertex arrays and a per-VERTEX colour whose alpha is
recomputed on the CPU every frame (the rim moves as the camera moves):

* **Rim (Fresnel-style) falloff**: `c = dot(normal, direction to camera)`; `alpha = rim_alpha * (1 - |c|)^rim_power`. Edge-on vertices (the
  limb from outside, the horizon from inside) are bright; vertices facing the camera (straight down, or straight up from inside) are clear.
  This is the cheap stand-in for real scattering: the light path through the air is longest at grazing angles.
* **Day/night**: multiplied by a smooth factor from `dot(normal, direction to sun)`: full on the day side, wrapping a little past the
  terminator (twilight), a 15 % floor on the night side.
* **Distance fade**: full within 70 % of the range, smoothly to 0 at the range (no popping).
* **Colour**: blue-shifted body colour (the old game's formula: `r*0.4+0.2, g*0.4+0.3, b*0.3+0.5`).
* **Blending**: additive (`SRC_ALPHA, ONE`): a glow needs no sorting. **Depth test ON, depth write OFF**: the shell is hidden by the planet
  itself, the ship, stations, rocks - anything in front of it - and never hides anything. From outside the far half is culled (it would only
  double the glow over the disc); from inside both halves draw (every face is sky).
* **Only near, real-geometry planets**: moons and the sun get none; a planet farther than `range_factor` radii, or drawn by star_system as the
  depth-less far backdrop (beyond 0.75 x far plane), is skipped.

Why these numbers: terrain peaks reach 1.03 x the radius, so the shell at 1.06 always clears them but stays a thin band on screen
(a thick shell looks like a glass bubble). 12 radii: at that distance the planet is ~10 degrees across and the rim is still a few pixels;
beyond it the glow is sub-pixel and not worth the fill.

## Inside the atmosphere
While the camera is inside a shell, a full-screen quad in the atmosphere colour is blended over the 3D world in a later pass (order 150,
after all world geometry, before the cockpit at 800), with its own depth test off - nothing else's depth testing changes. Its alpha is 0 at the
shell boundary and eases up to `inside_tint_max` at the surface (more air above you = thicker haze; starting at 0 means crossing the boundary never pops).
The cockpit interior is drawn after it and stays untinted (the haze is outside the canopy).

## Tunables (`config/game.json`, key `atmosphere.*`)
| key | default | meaning |
|---|---|---|
| `atmosphere.enabled` | true (Low preset: false) | draw atmospheres |
| `atmosphere.range_factor` | 12 | drawn within this many planet radii |
| `atmosphere.shell_radius_factor` | 1.06 | shell radius / planet radius (1.035-1.3) |
| `atmosphere.inside_tint_max` | 0.12 | tint alpha at the surface; 0 = no tint pass |
| `atmosphere.rim_alpha` | 0.85 | glow at the limb |
| `atmosphere.rim_power` | 3 | falloff exponent (higher = thinner limb) |
| `atmosphere.segments` | 24 | shell slices; stacks = 2/3; vertices (s+1)(2s/3+1) |

Quality presets (`core/quality/quality_rules.h`): `atmosphere.enabled` 0/1/1/1 and `atmosphere.segments` 16/24/32/48 (Low/Medium/High/Ultra:
0 / 425 / 792 / 1617 vertices per shell). Dev flags: `--force-atmosphere` (on even at Low), `--atmosphere-dump` (log the planets' positions and radii).

## Verification (2026-09-22, seed 1234, Planet 1 radius 408, saved games made by `logs/w49_mksaves.py`, cockpit camera, 800x600, Ultra)
* **6 radii out** (`logs/w49_far*.png`): luminance added by the atmosphere per radial band from the disc centre (on minus `--disable`):
  0.0 at the centre out to 60 % of the disc radius, +0.5 at 60 %, +2.4 at 75 %, +9 at 90 %, **+20 average (+70 max) at the limb**. The centre
  of the disc is untouched, the limb glows.
* **1.6 radii, straight over the lit face** (`logs/w49_close.png`): a clear disc with a glowing ring around it.
* **Inside the shell, 1.045 radii, looking at the horizon** (`logs/w49_inside*.png`): the sky near the horizon gains +36 R / +42 B, the sky
  higher up +13 R / +16 B, the ground only +2-5 (just the tint: the shell below the horizon is hidden by the ground, correct depth
  testing), and the cockpit panel is identical to the `--disable` run.

## Cost
Dev PC (RTX 2060 Super, Ultra, 1617 vertices, 1.6 radii from the planet): pass `world/atmosphere` 0.056 ms, tint 0.003 ms (`--profile`).
llvmpipe laptop stand-in (`taskset -c 18,19`, 2 threads, `--quality=low --force-atmosphere`, 16 segments, same save, 3 runs of 6 s):
**12.26 ms/frame on vs 10.93 ms off (+1.3 ms, 1% low 43 vs 51 fps)**; the pass itself is 0.05 ms of CPU, the rest is blended fill of a
shell that covers most of the screen. Hence Low = off. Far from planets the module costs nothing (no planet in range = no draw).
