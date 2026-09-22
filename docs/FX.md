# Particles (`fx/particles`)

A small, cheap particle system: engine exhaust, hit sparks, collision debris and a warp flash, and a typed event any module can use to spawn its own effects (weapons, mining, docking...).
Code: `package/modules/fx/particles/` (`particles_rules.h` is pure and unit-tested by `package/tests/test_particles.cpp`, `particles.cpp` is the module, `particles_api.h` the event).

## Asking for an effect (event API)
```cpp
#include "fx/particles/particles_api.h"
eng.events.emit(fx::SpawnParticles{"spark", {x, y, z}, {0, 1, 0}, {vx, vy, vz}, 20});
// kind, world position (double), direction of the burst (0,0,0 = every direction), velocity of the emitter (added to each particle), count (0 = the preset's own range)
```
Optional fields: `size` (multiplier, 0 = the preset's), `lifetime` (multiplier), `colour` (rgb 0..1; negative = the preset's own colours; a colour override fades to half brightness). Unknown kinds are ignored with one warning. With the module off nothing listens and nothing costs anything.

## Sources built in (read-only: nothing in `ship_core`, `warp_drive`, the cockpit or physics was changed)
| Effect | Trigger | Details |
|---|---|---|
| `exhaust` | thrust / strafe / lift actions (`core::IInput`) | chase view only (`core::ICamera::showsShip()`); 60 particles per second at full thrust times `fx.exhaust`. Forward thrust: from each `@THRUST-JET` emitter of the ship model (`cockpit::IShipModel::thrusters()`, docs/COCKPIT.md), along its jet normal, the particles split between the emitters, and each burst's SPAWN POSITION is spread across the emitter's own real face (a random point over the disc of its measured `radius x 0.8`, perpendicular to the jet normal - `fx::randomDiscOffset`) instead of every particle leaving from one exact point; without the service or the tag the fallback constants `fx.nozzle_back` 7.9 / `fx.nozzle_down` 0.4 (ShipV2 thrusters, no surface to spread across: a single point) and the direction opposite to the total push. Strafe / lift / reverse only: from the ship's centre, opposite to the push; carries the ship's velocity |
| `spark` + `debris` | `core::Collided` | at the contact point, flying back along the contact normal; count grows with the closing speed (3 at a scratch to 40 at 200 m/s; debris a third of that) |
| `spark` | `ship::DamageTaken` | 2-10 sparks around the ship, scaled by the damage |
| `warp_flash` | `ship::IWarpDrive::engaged()` turning true (polled every frame) | 50-80 blue-white glow particles bursting out 8 units ahead of the ship |

## Presets (`data/particles.json`)
One object per preset, read through `core::IData` (category `particles`); built-in defaults with the same values are used when the file is missing, and fields left out keep the built-in value. Shipped presets: `exhaust`, `spark`, `debris`, `warp_flash`, `muzzle` (not used yet: for weapons).

| Field | Meaning |
|---|---|
| `count_min`, `count_max` | particles per burst (random in the range unless the event gives `count`) |
| `speed_min`, `speed_max` | m/s relative to the emitter |
| `life_min`, `life_max` | seconds |
| `size_start`, `size_end` | quad half size in world units, linear over the life |
| `color_start`, `color_end` | `[r, g, b, a]` at birth and death, linear (fade out with alpha 0 at the end) |
| `drag` | velocity decay per second (`v *= exp(-drag dt)`) |
| `additive` | true = glow (adds light, drawn after the alpha-blended ones), false = alpha blend |
| `spread` | half angle in degrees of the cone around the emit direction (180 = every direction) |

**Adding an effect:** add an entry to `data/particles.json` (e.g. `"mining_dust": {...}`) and emit `fx::SpawnParticles{"mining_dust", ...}` from your module. Values are sanitised (no negative ranges, colours clamped).

## How it stays cheap
* **Pool:** fixed capacity (`fx.max_particles`), struct-of-arrays, **no heap allocation after init**; dead particles are swap-removed so the live ones stay contiguous. A full pool or an over-budget frame just drops the excess spawns (counted, logged at debug).
* **Per-frame spawn budget** (`fx.spawn_budget`), distance culling (`fx.max_distance`, 2,500), nothing behind the camera and nothing closer than 1.5 units to it (in cockpit view the ship's own exhaust is never drawn).
* **One pass** (`fx/particles`, order 70: after the star system, asteroids and stations, before the cockpit): camera-relative quads (double subtract, then float) built into preallocated arrays, then at most **two `glDrawArrays(GL_QUADS)`** calls (alpha-blended, then additive). Depth test on, depth write off, no lighting; all state restored.
* **Soft dots:** one shared 32x32 RGBA texture (a round soft dot) made once; `fx.soft_dots false` gives plain squares without a texture.
* **Particles freeze while the game is paused.**

Measured on the dev machine (RTX 2060 SUPER, `-O2`): 2,000 live particles cost 0.046 ms per frame for update + quad building (unit test); in the game 820 live particles cost 0.03 ms CPU for update + build + submit.
Software renderer (`llvmpipe`, 2 pinned cores, `--profile=gpu`, `--quality=low`, ~800 live particles): the pass takes about 0.5 ms with plain squares and about 0.8 ms with soft dots, of a ~19 ms frame (the first textured draw once costs ~28 ms: driver setup).

## Tunables and the switches for a laptop A/B (`config/game.json`)
| Key | Default | |
|---|---|---|
| `fx.enabled` | true | **everything off** (no pass, no listeners) |
| `fx.max_particles` | 800 (quality preset: 300 / 800 / 1500 / 3000) | pool size |
| `fx.exhaust` | 1.0 (preset: 0.5 / 1 / 1 / 1) | **exhaust amount: 0 = off**, 0.5 = half |
| `fx.spawn_budget` | 150 (preset: 60 / 150 / 250 / 400) | spawns per frame |
| `fx.size_scale` | 1.0 | multiplier on every size |
| `fx.soft_dots` | true | **false = no texture, hard squares** |
| `fx.max_distance` | 2500 | draw distance |

To A/B on the laptop: `fx.enabled false` (nothing), then `fx.exhaust 0` with the rest on (sparks, debris, flash only), then `fx.soft_dots false` (texture cost), then `fx.max_particles 300`. Compare `pass:fx/particles` in `--profile=gpu` and the `--benchmark` fps.

## Dev flags
`--fx-test=FRAME` spawns a row of spark / debris / warp flash / exhaust bursts 15 units in front of the ship on that frame (for screenshots); `--fx-test-loop` repeats it every 10 frames (steady-state cost measurements); `--fx-test-thrust` makes the exhaust source behave as if full forward thrust were applied (exhaust without a keyboard). Chase view: `{"camera.mode": 1}` in a `--settings=` file. At debug log level the module prints live particles, quads drawn, drops and CPU per frame every 2 s.
