# Orbit lock (`ship/orbit_lock`)

`toggle_orbit_lock` (key **O**) puts the ship on a stable circular orbit around the body whose **surface** is nearest (planet, moon or sun); pressing it again releases the ship.
Read with `IInput::pressed` in `onUpdate`, ignored while the game is paused. Newtonian rules for everyone: the module only uses `IShip::position()`, `velocity()`, `status()` and `setVelocity()`; nothing in `ship_core` or the `IShip` contract changed.

## Aligning with the guide (3.5c)
Near a body (within `orbit.guide_range` = 6 radii above the surface) the game draws the orbit the lock WOULD give from your current state, and O only locks when you are lined up with it.
- **The guide:** a thin ring (one `GL_LINE_STRIP`, 96 segments, camera-relative, additive, depth-tested, fading with distance) around the body through your position, in the plane containing your position and your velocity relative to the body (if you are not moving: your heading, then world up). The brighter arc is the next ~90 degrees **ahead** of you along the way you are going (retrograde works: the ring follows your motion); small ticks every 30 degrees (not on Low); a short line at the ship showing where the orbit would take you (amber/green) and a white one showing where you are actually going. **Amber = misaligned, green = aligned.** Hidden while locked, docked/docking, warping or dead.
- **Aligned means:** relative speed within `orbit.align_speed_tol` (12 %) of the circular speed `v = sqrt(mu / r)` (same gravity model, `orbit.gravity_scale`), and the angle between your relative velocity and the orbit tangent within `orbit.align_angle_tol` (10 degrees): fly **parallel to the ring at the right speed**, not towards or away from the body. Altitude within the engage band as before (`orbit.min_altitude`, `orbit.engage_range`).
- **O when misaligned** refuses, logs why and emits `ship::OrbitLockRefused{bodyName, reason}`: "speed +23 m/s too fast (need 84)", "speed -30 m/s too slow (need 84)", "heading 34 degrees off the orbit (fly tangent)", "too close to the surface", "too far from the body". Checked in that order (altitude, heading, speed). `orbit.require_alignment` false restores the old snap from any state (testing).
- **Settle:** on a lock the ship's velocity is blended from its own to the circular one over `orbit.settle_seconds` (1.5 s) and its distance to the orbit radius eased the same way; then the analytic hold takes over exactly where the ship is (no jump). Measured (start 17 m/s slow, 2.5 units off): radius error 2.5 -> 1.2 -> 0.3 -> 0.06 -> 0.005 -> 0.000 and speed error 16.6 -> 15.2 -> 9.3 -> 4.9 -> 1.5 -> 0.18 m/s at 0.25 s steps (logged at info level as `settle ...`). Radius is then constant to the last digit. `orbit.settle_seconds` 0 = snap.
- **HUD:** a block at the top right (drawn by this module through `core::UIHandler`, so `ui/ship_hud` is untouched): "ORBIT PLANET 1  alt 1.0K  need 84 m/s" and "SPEED +12  HEADING 4 deg  [ALIGNED - press O]" (green) or "[<reason>]" (amber). The service `ship::IOrbitGuide` (`orbit_lock_api.h`: `active()`, `bodyName()`, `altitude()`, `neededSpeed()`, `speedError()`, `headingErrorDeg()`, `aligned()`, `refuseReason()`) exposes the same for the HUD and a later cockpit radar.
- **No gravity yet:** the ship is not pulled by bodies; the guide is what the lock would give (docs/QUESTIONS.md, item 14).
- **Tunables:** `orbit.show_guide` true, `orbit.guide_range` 6, `orbit.require_alignment` true, `orbit.align_speed_tol` 12, `orbit.align_angle_tol` 10, `orbit.settle_seconds` 1.5, `orbit.guide_segments` 96 (quality preset: Low 48 and no ticks).
- **Cost** (dev machine, `--profile`): the ring pass 0.009 ms (96 segments) / 0.007 ms (48), the HUD block about 0.013 ms of the UI pass; two draw calls (ring, ticks + markers), no per-frame allocation (buffers sized once; the block's strings refresh 10 times a second).
- **Seen edge-on:** the ring's plane contains your position and velocity, so from the cockpit (and nearly from chase view) it looks like a line through the horizon; tilt away from the tangent to see it open up. The two markers show the comparison.
- Code: pure rules `guide*` / `checkAlignment` / `settleStep` in `orbit_lock_rules.h`, tests in `package/tests/test_orbit_guide.cpp`.

## Engaging
Needs: ship alive, not warping, a body within `orbit.engage_range` radii above its surface (default 3) and at least `orbit.min_altitude` (20 units) above it, **and (default) aligned with the orbit, see above**. Otherwise it logs why and does nothing.
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
