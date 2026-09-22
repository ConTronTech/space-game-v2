# Ship gravity (ship/gravity, task 3.5e)

Module: `package/modules/ship/gravity/` (`gravity.cpp` wiring, `gravity_rules.h` pure logic, tests in `package/tests/test_gravity.cpp`). Rationale: docs/PHYSICS_RESEARCH.md section 3 and docs/QUESTIONS.md #14.

## The model: single-body sphere of influence ("patched conics", as in Kerbal Space Program)
- The ship feels gravity from **exactly one body at a time**: the innermost sphere of influence (SOI) it is inside. A moon's SOI wins over its planet's, which wins over the sun's. Among equals the nearest wins.
- The sun is the fallback out to `gravity.sun_range` x the outermost planet's orbit radius (default 2x). Beyond that, **no gravity at all**: deep space stays pure Newtonian float.
- Planets, moons and the sun keep their analytic, driftless orbits (world/star_system) **unchanged**. Nothing ever pulls on them.
- Same gravity constant as the orbit lock/guide: `mu = orbit.gravity_scale * radius^2` (`orbit::bodyMu`, the formula is shared, not copied). So the guide's circular speed `sqrt(mu / r)` is now a real orbit. The surface gravity of every body is therefore `orbit.gravity_scale` (60 m/s^2 by default).
- SOI (Laplace): `r_soi = a * (mu_body / mu_parent)^(2/5)`, where `a` is the body's orbit radius around its parent (the sun for planets, the planet for moons). With this mu that is `a * (r_body / r_parent)^0.8`.
- Acceleration: `a = -mu / max(r, gravity.min_radius)^2 * unit(ship - body)`, `|a|` capped at `gravity.max_accel`. At the exact centre: zero (never NaN).
- Integration: every fixed step the module adds `a * dt` to the ship's velocity with `IShip::setVelocity`, and ship_core then does `position += velocity * dt`. That is semi-implicit (symplectic) Euler: a circular orbit keeps its radius over many periods, no secular drift. ship_core is not modified.
- Not modelled (on purpose): the sun's pull on the ship while inside a planet's SOI, and the planet frame's own acceleration around the sun (about 0.003 m/s^2 for Planet 1 - a few units of drift per minute, invisible in play).

## SOI radii for seed 1234 (6 planets, scale 60, sun range 2x)
Worked example, Planet 1: radius 408, orbit 46,493 around the sun (radius 1,781): `46493 * (408 / 1781)^0.8 = 14,305` units. Circular speed at 1,020 from its centre: `sqrt(60 * 408^2 / 1020) = 99.0 m/s`, local g 9.6 m/s^2.

| Body | Radius | Orbit radius | mu | SOI |
|---|---|---|---|---|
| Sun | 1781 | - | 1.904e8 | 704,399 (sun range) |
| Planet 1 | 408 | 46,493 | 9.998e6 | 14,305 |
| Planet 2 | 198 | 90,501 | 2.344e6 | 15,587 |
| Planet 3 | 355 | 140,369 | 7.563e6 | 38,626 |
| Planet 3 Moon 1 | 44 | 1,319 | 1.147e5 | 247 |
| Planet 3 Moon 2 | 56 | 1,445 | 1.908e5 | 332 |
| Planet 4 | 494 | 218,380 | 1.464e7 | 78,262 |
| Planet 4 Moon 1 | 126 | 1,543 | 9.455e5 | 516 |
| Planet 4 Moon 2 | 145 | 1,937 | 1.259e6 | 726 |
| Planet 5 | 701 | 280,726 | 2.95e7 | 133,148 |
| Planet 5 Moon 1 | 48 | 2,188 | 1.387e5 | 256 |
| Planet 6 | 1134 | 352,200 | 7.722e7 | 245,482 |
| Planet 6 Moon 1 | 169 | 3,125 | 1.718e6 | 682 |
| Planet 6 Moon 2 | 252 | 3,596 | 3.798e6 | 1,078 |
| Planet 6 Moon 3 | 182 | 3,798 | 1.994e6 | 880 |

Note: the moons' SOIs are small (a few hundred units above their surfaces) because they orbit close to big planets. The orbit guide shows moon orbits out to 2,500 units; above a moon's SOI the ship actually orbits the planet, so a free (unlocked) orbit around a moon only works close in. The lock itself is unaffected.

## Tunables (config/game.json)
| Key | Default | Meaning |
|---|---|---|
| `ship.gravity_enabled` | **true** | Gravity on (user decision 2026-09-21: a core feature, on from the start). **false = exactly the pre-gravity (main) flight feel**: the module never touches the ship. Keep it as the fallback / regression check. |
| `orbit.gravity_scale` | 60 | Reused, not new: `mu = scale * radius^2`. |
| `gravity.min_radius` | 0 | Gravity stops growing inside this distance from a centre; 0 = the body's own radius. |
| `gravity.max_accel` | 200 | m/s^2 cap. The strongest real value is the surface gravity (= gravity_scale, 60), so 200 never limits a real orbit or fall; it only stops a degenerate case (min_radius set tiny, ship at a centre) from launching the ship in one step. |
| `gravity.soi_hysteresis` | 1.05 | The dominant body is kept until the ship is 1.05x its SOI away (entering a SOI uses the real edge): no flicker at a boundary. |
| `gravity.sun_range` | 2 | The sun pulls out to this many times the outermost planet's orbit radius. |

## Interactions
- **Orbit lock** (docs/ORBIT_LOCK.md): while locked, gravity is **not** applied: the lock already steers the ship on the exact analytic circle with `setVelocity`; adding gravity would fight it. The lock state comes from the existing `ship::OrbitLockChanged` event (no change to orbit_lock was needed). When the lock releases, the ship leaves with the circular velocity and simply stays in that orbit under real gravity.
- **Docking**: skipped while `ship::IDocking::busy()` (approach or docked: the ship is held).
- **Warp**: skipped while `status().warping`.
- **Dead / paused**: skipped (fixed steps do not run while paused anyway).
- **Collisions**: unchanged; a ship that falls onto a planet hits it through the normal physics world (confirmed: `hit planet (radius 408) at 113.3 m/s closing: impact, damage 28.3`, then it bounces).
- **Respawn**: the dominant body is re-chosen from scratch.

## Debugging
- `engine.log_level: debug`: once per second while gravity acts, `Planet 1: distance 1019 (altitude 610), soi 14305, g 9.636 m/s^2, energy -4902.5, eccentricity 0.002` (computed only when debug logging is on), plus a line whenever the dominant body changes.
- core/debugger (F6, docs/DEBUGGER.md), optional: watches `gravity.dominant`, `gravity.accel`, `gravity.local_g`, `gravity.soi`; draw hook `gravity.draw` = the dominant body's SOI circle (blue, in the ship's orbital plane), the acceleration vector (red) and a 60-segment forecast arc (yellow, same symplectic scheme at 0.5 s steps, fixed buffers, no allocation). Dominant-body changes go to the debugger's event log. Without the debugger the module registers nothing and costs nothing extra.
- Dev flags: `--gravity-test=FRAME` (one info line: dominant body, distance, SOI, acceleration at that frame); `--auto-gravity` (force on) / `--no-gravity` (force off) without editing game.json; `--gravity-scenario=orbit|drop|deep` places the ship at frame 5 and logs altitude/speed every simulated second (`orbit`: 1.5 radii above body `--gravity-body=N`, default 1 = Planet 1, at the guide's circular speed; `drop`: same spot at rest relative to the body; `deep`: 2.5x the sun range out, drifting at 10 m/s).

## How it was tested (3.5e)
`SDL_AUDIODRIVER=dummy ./space_game_v2 --frames=2600 --gravity-scenario=orbit`: altitude 612.3 -> 610.2 -> 611.7 over 43 s (eccentricity 0.002, speed 98.98-99.16 m/s): a real orbit. `drop`: 612 -> 12 altitude in 10 s, speed 0 -> 145 m/s, then the planet impact above. `deep`: speed 10.0000 m/s for 38 s, dominant none. `--no-gravity` + `drop`: altitude 612.3 for 14 s (no pull). Orbit lock (`--auto-orbit-lock=60` after the orbit scenario): locked radius 1020.392-1020.427 with gravity on vs 1031.361-1032.484 (settle included) with it off - equally stable. Unit tests: `make test` (gravity_*).
