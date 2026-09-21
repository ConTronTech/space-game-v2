# Stations and docking (`world/stations`, `ship/docking`)

Simple on purpose (user decision 2026-09-21): a **cube** with a flat **landing pad** (a flattened cylinder) on top is the placeholder station until a proper model exists (the old OBJs in `assets/models/stations` are not loaded); docking is a **key press** (`dock`, **G**), not automatic; and docking gives
**no free refuel, shield or repair** (fuel and shields will come from mining). A test-only tunable can refill (see below).

## `world/stations` (pass `stations`, order 65: after the asteroids)
Provides `world::IStations` (`stations_api.h`): `count()`, `info(i)` -> `{name, kind (Orbital|Planetary), position (double), velocity, radius (dock zone), scale, half, up, parent, forward, spinRate, padTop, padRadius}`, `nearest(pos)`.
Positions and velocities are current: recomputed every fixed step from the star system's simulation time (`IStarSystem::simTime()`), so saved games, `world.time_scale` and pausing all just work.

**Generation** (`stations_rules.h`, pure, seeded by `world.seed`, ported from the old game): `stations.count` (default 2, 1-3) stations, each on a random planet, 50/50 orbital or planetary. Scale 2, or 3 around planets bigger than 600 units; the dock zone is `scale * 60` units from the station centre (120 / 180); the model is a platform 40-60 units across (`half = scale * 10`).
- **Orbital:** circles the planet like a moon on a circular, analytic orbit tilted by up to 10 degrees, outside the planet's moons **and** outside the asteroid cluster shell (`4.5 R + 500 .. + max(400, 3 R)`; the formula is replicated from `world/asteroids`, not read from `IAsteroids`, so the modules stay independent). Speed about 5-15 units/s. It also spins slowly (visual only).
- **Planetary:** at a fixed latitude within +-60 degrees and a random longitude on the planet, raised above the tallest terrain (`1.02 R + 3.4 half`), oriented along the surface normal. **Planets do not rotate in V2**, so it never moves relative to its planet; it still travels with the planet around the sun (10-15 units/s).
- `stations.seed_offset` (default 0) is added to `world.seed` for the stations only: re-rolls where they are without changing the planets (the default seed 1234 gives two surface stations on Planet 2; offset 2 gives two orbital stations around Planet 1).

**Model** (built once, no assets, about 150 triangles per station): the **cube** (half extent `half`, centred on `info().position`) is the base. On its top face (the `up` side) sits the **landing pad**: a cylinder flattened to a disc, radius `1.5 half` (wider than the cube), thickness `0.16 half`
(0.08 x the cube's size), so the pad top is `padTop = 1.16 half` above the centre. The pad is dark grey with a lighter yellow inner disc (the landing mark) and a dark bar toward the station's local forward (it makes the spin and the ship's heading readable).
Orbital: the WHOLE station spins about `up` (the orbit-plane normal) at `spinRate`. Planetary: the cube stands on a pedestal reaching into the ground, pad on top, `up` = the surface normal, no spin. `info().position` is still the centre of the cube (unchanged meaning: the radar diamond and the dock zone use it).
**Frame:** `up`, `forward` (the spin reference axis at the CURRENT spin angle; it turns at `spinRate`) and `right = up x forward` form a right-handed frame (`world::referenceForward`, `spunForward` in `stations_rules.h`).
Beyond `stations.draw_distance` (15,000) it is a 4 px dot (pulled in along the line of sight like the star system's far bodies, so it stays inside the far plane).

**Physics:** each station is a static `station` body (radius `1.6 half`, 32 / 48 units) moved every step with `setBody(pos, velocity)` (a jump such as a loaded game teleports instead). Ship damage uses the existing `other` tier (`ship.damage_other`, default 10 at 200 m/s, 1 HP minimum) with the normal bounce. While the ship is approaching or docked, `ship_core` ignores collisions with `station` bodies (see docs/SHIP.md, `setHeld`).
The pillar of a planetary station has no collision.

Tunables: `stations.enabled` true, `stations.count` 2, `stations.seed_offset` 0, `stations.draw_distance` 15000.

## `ship/docking` (the `dock` action, G)
- **Dock** (`IInput::pressed` in `onUpdate`, ignored while paused): needs the ship alive, not warping, **not orbit-locked** (refused with a log line: press O first), inside the nearest station's dock zone and **slower than `docking.max_speed` (15 m/s) relative to the station**. Otherwise one log line says why (`too far from the station`, `too fast`, `warp drive engaged`, `orbit lock engaged`, ...).
- **Approach (G):** all the old conditions are unchanged. The ship does not snap: for `docking.approach_seconds` (default 1.5 s) it glides from where it is onto the pad (smoothstep on the position, a normalised blend of forward and up for the orientation), the target being recomputed every step from the moving station.
  The `Docked` event (and so the HUD's DOCKED banner and `IDocking::docked()`) comes at the END of the approach; during it `nearestDockable` says "already docked" and thrust cancels it (no Undocked event, the ship keeps flying). `approach_seconds` 0 = instant.
- **Docked = sitting ON the pad, like the old game:** the ship's origin is `docking.rest_height` (1.4: ShipV2's belly is 1.38 below the eye) above the pad top at the pad centre, its `up` = the station `up` (belly to the pad), its forward = the heading it had at G **projected onto the pad plane and kept in the station's frame** (`world::captureHeading`), so it turns with an orbital station's spin.
  Every fixed step: `pose(t) = stationPose(t) x fixed local offset` (`world::padPose`), analytic, so nothing drifts; the ship's velocity is set to the pad point's true velocity `v_station + spinRate * up x r` (`world::padPointVelocity`), so the speed readout, the radar and the undock momentum agree.
  The camera keeps interpolating between steps, so the view stays smooth while the pad moves. Player rotation and thrust input are ignored while held (`IShip::setHeld`); a loaded game releases the hold (the saved position wins).
- **Undock:** press G again, or thrust / strafe / lift (dead zone 0.1), or damage, death, respawn, or the warp drive engaging. The ship leaves with the velocity of the pad point under it (station velocity + spin) plus `docking.undock_push` (default 3 m/s) straight out from the station (on the pad centre the spin adds nothing, so it equals the station velocity + the push); control returns at once. Turning does not undock.
- **Events / service** (`docking_api.h`): `ship::Docked{stationName}`, `ship::Undocked{stationName}`; `ship::IDocking`: `docked()`, `stationName()`, `nearestDockable(name, distance, ok, reason)` returns false when there is no station, else the nearest one, its distance from the centre, whether G would work now and why not. The HUD shows it (see the HUD section below).
- **Not saved:** the docked state is not saved (after loading you are flying with whatever velocity you had; the station list comes from the seed).
- **`docking.test_refill`** (default **false**, TEST ONLY): while docked, heal to full, refill warp fuel and (re)install a full shield. A warning is logged at startup when it is on. Real refuelling and shields are meant to come from mining asteroids.

Tunables: `docking.max_speed` 15, `docking.undock_push` 3, `docking.approach_seconds` 1.5, `docking.rest_height` 1.4, `docking.test_refill` false.

## Dev flags
`--auto-dock=FRAME` presses the dock key on that engine frame; `--auto-undock=FRAME` undocks then (no-op if not docked). Only active when given. Saved-game method as in docs/WARP.md; at `engine.log_level: debug` the module logs once per second while docked: the distance to the pad point, the angle between the ship's up and the station's up, and the speed relative to the pad. Measured on an orbital station (moving 21 m/s and spinning) and a planetary one over 15 s: distance 0.001-0.004 units (float precision of the ship position at 40,000 units from the origin), up angle 0.00000 degrees, relative speed 0.0001 m/s.

## Code
Pure rules (generation, analytic positions, docking checks, the station frame, pad pose, pad point velocity, heading capture, approach easing and orientation blend, undock velocity): `stations_rules.h`, tested in `package/tests/test_stations.cpp` (including a one-minute simulation of a moving, spinning orbital and a moving planetary station: no drift, up within 1e-4, relative speed ~0).

## HUD
`ui/ship_hud` shows "STATION 1  420 m" plus a green "DOCK [G]" (or the short reason) near a station, and "DOCKED: STATION 1 - [G] UNDOCK" plus DOCKED/UNDOCKED banners while docked: see docs/HUD.md. Tunable `docking.prompt_range` (0 = 4 x the dock radius).

## Radar
The cockpit's PROXIMITY_RADAR (docs/COCKPIT.md, "Radar contacts") shows every station as a cyan diamond on the scope, with the same logarithmic range and height stems as planets: hollow normally, **filled for the station you can dock at right now**
(the same condition as the HUD's `DOCK [G]`, from `ship::IDocking::nearestDockable`). The nearest station and its surface distance are printed under the scope (`STATION 1  96`). It reads `world::IStations` per draw, so it needs no station-side code and simply shows nothing without the module.
