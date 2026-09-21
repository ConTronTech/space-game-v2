# Stations and docking (`world/stations`, `ship/docking`)

Simple on purpose (user decision 2026-09-21): a **cube** and a **cylinder** are the placeholder platform until a proper model exists (the old OBJs in `assets/models/stations` are not loaded); docking is a **key press** (`dock`, **G**), not automatic; and docking gives
**no free refuel, shield or repair** (fuel and shields will come from mining). A test-only tunable can refill (see below).

## `world/stations` (pass `stations`, order 65: after the asteroids)
Provides `world::IStations` (`stations_api.h`): `count()`, `info(i)` -> `{name, kind (Orbital|Planetary), position (double), velocity, radius (dock zone), scale, half, up, parent}`, `nearest(pos)`.
Positions and velocities are current: recomputed every fixed step from the star system's simulation time (`IStarSystem::simTime()`), so saved games, `world.time_scale` and pausing all just work.

**Generation** (`stations_rules.h`, pure, seeded by `world.seed`, ported from the old game): `stations.count` (default 2, 1-3) stations, each on a random planet, 50/50 orbital or planetary. Scale 2, or 3 around planets bigger than 600 units; the dock zone is `scale * 60` units from the station centre (120 / 180); the model is a platform 40-60 units across (`half = scale * 10`).
- **Orbital:** circles the planet like a moon on a circular, analytic orbit tilted by up to 10 degrees, outside the planet's moons **and** outside the asteroid cluster shell (`4.5 R + 500 .. + max(400, 3 R)`; the formula is replicated from `world/asteroids`, not read from `IAsteroids`, so the modules stay independent). Speed about 5-15 units/s. It also spins slowly (visual only).
- **Planetary:** at a fixed latitude within +-60 degrees and a random longitude on the planet, raised above the tallest terrain (`1.02 R + 3.4 half`), oriented along the surface normal. **Planets do not rotate in V2**, so it never moves relative to its planet; it still travels with the planet around the sun (10-15 units/s).
- `stations.seed_offset` (default 0) is added to `world.seed` for the stations only: re-rolls where they are without changing the planets (the default seed 1234 gives two surface stations on Planet 2; offset 2 gives two orbital stations around Planet 1).

**Model:** built once, no assets: a lit cube (habitat); orbital = cube with a spinning cylinder hub (two crossed arms), planetary = cube on a cylinder pillar. Flat kind colours, one push/multiply per station, camera-relative (double subtract first), all GL state restored.
Beyond `stations.draw_distance` (15,000) it is a 4 px dot (pulled in along the line of sight like the star system's far bodies, so it stays inside the far plane).

**Physics:** each station is a static `station` body (radius `1.6 half`, 32 / 48 units) moved every step with `setBody(pos, velocity)` (a jump such as a loaded game teleports instead). Ship damage uses the existing `other` tier (`ship.damage_other`, default 10 at 200 m/s, 1 HP minimum) with the normal bounce; `ship_core` is unchanged.
The pillar of a planetary station has no collision.

Tunables: `stations.enabled` true, `stations.count` 2, `stations.seed_offset` 0, `stations.draw_distance` 15000.

## `ship/docking` (the `dock` action, G)
- **Dock** (`IInput::pressed` in `onUpdate`, ignored while paused): needs the ship alive, not warping, **not orbit-locked** (refused with a log line: press O first), inside the nearest station's dock zone and **slower than `docking.max_speed` (15 m/s) relative to the station**. Otherwise one log line says why (`too far from the station`, `too fast`, `warp drive engaged`, `orbit lock engaged`, ...).
- **While docked** the ship is held at the offset it had when it docked (nothing jumps) and moves with the station: each fixed step it is steered through `IShip::setVelocity` to `station position + station velocity * dt + offset`, exactly like `ship/orbit_lock` (analytic, no drift; `ship_core` unchanged). Orbital or planetary alike. Measured: distance to an orbital station 112.380-112.382 units for 7 s while the station moved about 140 units.
- **Undock:** press G again, or thrust / strafe / lift (dead zone 0.1), or damage, death, respawn, or the warp drive engaging. The ship leaves with the station's velocity plus `docking.undock_push` (default 3 m/s) straight out from the station. Turning does not undock.
- **Events / service** (`docking_api.h`): `ship::Docked{stationName}`, `ship::Undocked{stationName}`; `ship::IDocking`: `docked()`, `stationName()`, `nearestDockable(name, distance, ok, reason)` returns false when there is no station, else the nearest one, its distance from the centre, whether G would work now and why not. The HUD shows it (see the HUD section below).
- **Not saved:** the docked state is not saved (after loading you are flying with whatever velocity you had; the station list comes from the seed).
- **`docking.test_refill`** (default **false**, TEST ONLY): while docked, heal to full, refill warp fuel and (re)install a full shield. A warning is logged at startup when it is on. Real refuelling and shields are meant to come from mining asteroids.

Tunables: `docking.max_speed` 15, `docking.undock_push` 3, `docking.test_refill` false.

## Dev flags
`--auto-dock=FRAME` presses the dock key on that engine frame; `--auto-undock=FRAME` undocks then (no-op if not docked). Only active when given. Saved-game method as in docs/WARP.md; at `engine.log_level: debug` the module logs the distance to the station and the relative speed once per second while docked.

## Code
Pure rules (generation, analytic positions, docking checks, steering, undock velocity): `stations_rules.h`, tested in `package/tests/test_stations.cpp`.

## HUD
`ui/ship_hud` shows "STATION 1  420 m" plus a green "DOCK [G]" (or the short reason) near a station, and "DOCKED: STATION 1 - [G] UNDOCK" plus DOCKED/UNDOCKED banners while docked: see docs/HUD.md. Tunable `docking.prompt_range` (0 = 4 x the dock radius).
