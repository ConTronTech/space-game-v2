# Orbit lock (`ship/orbit_lock`)

`toggle_orbit_lock` (key **O**) puts the ship on a stable circular orbit around the body whose **surface** is nearest (planet, moon or sun); pressing it again releases the ship.
Read with `IInput::pressed` in `onUpdate`, ignored while the game is paused. Newtonian rules for everyone: the module only uses `IShip::position()`, `velocity()`, `status()` and `setVelocity()`; nothing in `ship_core` or the `IShip` contract changed.

## Engaging
Needs: ship alive, not warping, a body within `orbit.engage_range` radii above its surface (default 3) and at least `orbit.min_altitude` (20 units) above it. Otherwise it logs why and does nothing.
The ship is put on a circular orbit at its **current** altitude: speed `v = sqrt(mu / r)` (r = distance from the body centre), tangential, in the plane through the body centre containing the ship's current motion relative to the body
(so you keep going round the way you were going). If there is no relative motion or it points straight at/away from the body, the plane comes from the ship's heading, then the world up axis.

**Gravity model** (a design, not real physics): `mu = orbit.gravity_scale * radius^2` (default scale 60), so bigger bodies pull harder. Examples: 400-unit planet at 1,000 units from its centre ~98 m/s;
the 1,800-unit sun at 5,400 ~188 m/s; a 50-unit moon at 150 ~31 m/s. One tunable, unit-tested.

## How the orbit is held
Each fixed step the ship's target position is `body position + start offset rotated about the orbit normal by omega * time_locked` (Rodrigues rotation of the offset taken at lock time, computed from scratch each step: no integration, **no drift**).
The body's next position is its current one plus its velocity (from the last two steps). Since the ship integrates `position += velocity * dt`, the module sets `velocity = (target - position) / dt` through `IShip::setVelocity`.
So the ship follows the moving planet and the radius stays constant for as long as you like. Side effects: while locked the ship's world-frame speed includes the body's own orbital speed (a planet moves ~10-15 units/s),
and far from the origin (float positions, 350,000 units out) the speed readout jitters by a fraction of a m/s.

## Releasing
The ship keeps whatever velocity it has (Newtonian: it leaves along the tangent). It is released by: pressing the key again; **thrust, strafe, lift or brake** (`orbit.release_on_thrust`, default true; a dead zone of 0.1; brake counts because the orbit would silently override it);
any damage (collision, hit), death, respawn; or the warp drive engaging. Turning (pitch/yaw/roll) does not release it.
The lock is **not saved**: after loading a game the ship is simply flying with the velocity it had (an orbital velocity if it was locked), and you press O again.

## Event for the HUD
`ship::OrbitLockChanged{locked, bodyName}` (`orbit_lock_api.h`) on every engage/release, e.g. `ORBIT LOCKED: PLANET 1`. `bodyName` is also set on release. The HUD/cockpit is not touched by this module.

## Tunables
`orbit.gravity_scale` 60, `orbit.engage_range` 3 (radii above the surface), `orbit.min_altitude` 20, `orbit.release_on_thrust` true.

## Dev flags
`--auto-orbit-lock=FRAME` and `--auto-orbit-lock-off=FRAME` act as if the key was pressed on that engine frame (toggles; ignored while paused). With `engine.log_level: debug` the module logs radius and relative speed once per second while locked.

## Difference from the old game
The old game's lock matched the planet's velocity (station-keeping) with a 2.5 s braking phase; this one is a real circular orbit around the body as designed for 3.5. Locking required speed <= 50 there; here any speed works.

## Code
Pure rules (no SDL/GL/engine): `orbit_lock_rules.h` (gravity, speeds, nearest body, range, plane fallback, position on orbit, release rule), tested in `package/tests/test_orbit_lock.cpp`.
