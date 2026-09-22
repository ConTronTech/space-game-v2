# Orbit lock (`ship/orbit_lock`)

`toggle_orbit_lock` (key **O**) puts the ship on a stable circular orbit around the body whose **surface** is nearest (planet, moon or sun); pressing it again releases the ship.
Read with `IInput::pressed` in `onUpdate`, ignored while the game is paused. Newtonian rules for everyone: the module only uses `IShip::position()`, `velocity()`, `status()` and `setVelocity()`; nothing in `ship_core` or the `IShip` contract changed.

## Aligning with the guide (3.5c)
Near a body (within max(`orbit.guide_range` x radius, `orbit.guide_min_range`) = max(6 radii, 2,500 units) above the nearest **surface**) the game draws, **with no key press** (pure rule `guideVisible`), the orbit the lock WOULD give from your current state, and O only locks when you are lined up with it.
- **The guide:** a thin ring (one `GL_LINE_STRIP`, 96 segments, camera-relative, additive, depth-tested, fading with distance) around the body through your position, in the plane containing your position and the **tangential** part of your velocity relative to the body. **Radial approach (3.5d):** when that part is under 20 % of your speed (or 2 m/s) the plane instead contains your nose, or (nose at the body) your RIGHT vector, then the world axes; it switches back only above 30 % (and 3 m/s), so it does not flip. The arc ahead and the tangent marker then point sideways (turn that way), never through the centre. The brighter arc is the next ~90 degrees **ahead** of you along the way you are going (retrograde works: the ring follows your motion); small ticks every 30 degrees (not on Low); a short line at the ship showing where the orbit would take you (amber/green) and a white one showing where you are actually going. **Amber = misaligned, green = aligned.** Hidden while locked, docked/docking, warping or dead.
- **Aligned means:** relative speed within `orbit.align_speed_tol` (**25 %**, was 12 %) of the circular speed `v = sqrt(mu / r)` (same gravity model, `orbit.gravity_scale`), and the angle between your relative velocity and the orbit tangent within `orbit.align_angle_tol` (**25 degrees**, was 10; a 20-degree error locks): fly **parallel to the ring at the right speed**, not towards or away from the body. Altitude within the engage band as before (`orbit.min_altitude`, `orbit.engage_range`). **Hysteresis (3.5d):** once aligned (green) it stays aligned while inside the tolerances x 1.25, and drops only after 0.3 s outside them (`updateAlignLatch`); O accepts the latched state. Leaving the altitude band drops it at once.
- **O when misaligned** refuses, logs why and emits `ship::OrbitLockRefused{bodyName, reason}`: each says what to do: "23 m/s too fast: slow to 84 m/s", "30 m/s too slow: speed up to 84 m/s", "turn 34 deg toward the arc ahead", "too close to the surface: climb above 20", "too far: fly within 1.2K of the surface" (same text in the HUD block). Checked in that order (altitude, heading, speed). `orbit.require_alignment` false restores the old snap from any state (testing).
- **Settle (reworked in 3.5d):** the ship's velocity is the circular velocity where it IS plus the entry error (relative velocity minus the circular one at the lock point, carried round with the orbit) faded out with a smoothstep over the settle time; no position snapping, so the velocity is continuous and the correction acceleration peaks at 1.5 x error / time (a curve, not a snap: 11-18 m/s^2 for a 22-degree entry, was up to 175 m/s^2 when the old version pulled the radius back each step). The settle time is `orbit.settle_seconds` (1.5 s) for a perfect entry, scaling up to 3 s at the edge of the tolerances (`settleSecondsFor`). An inward entry ends a little lower (never below the surface + `orbit.min_altitude`); the analytic hold is then re-based on the circle through the ship (`rebaseOrbit`, speed already circular: no jump). Measured at Planet 1 (20.7-degree entry, 2.74 s): radial speed -29.0, -27.0, -24.3, -20.6, -16.8, -12.8, -8.7, -5.4, -2.6, -0.6, -0.03 m/s at 0.25 s steps, radius 1397 -> 1356.8, then constant (`settle ...` / `settled on ...` at info level). `orbit.settle_seconds` 0 = snap.
- **HUD:** a block at the top right (drawn by this module through `core::UIHandler`, so `ui/ship_hud` is untouched): "ORBIT PLANET 1  alt 1.0K  need 84 m/s" and "SPEED +12  HEADING 4 deg  [ALIGNED - press O]" (green) or "[<reason>]" (amber). The service `ship::IOrbitGuide` (`orbit_lock_api.h`: `active()`, `bodyName()`, `altitude()`, `neededSpeed()`, `speedError()`, `headingErrorDeg()`, `aligned()`, `refuseReason()`) exposes the same for the HUD and a later cockpit radar.
- **Gravity (3.5e):** with `ship.gravity_enabled` (default true) the ship is really pulled by the dominant body (docs/GRAVITY.md, same `mu`), so the guide is now real physics: fly tangentially at the guide's speed and you stay on that orbit without locking. While locked, ship/gravity does not apply (the lock holds the exact circle). With gravity off, the guide is only what the lock would give, as before.
- **Tunables:** `orbit.show_guide` true, `orbit.guide_range` 6, `orbit.guide_min_range` 2500, `orbit.require_alignment` true, `orbit.align_speed_tol` 25, `orbit.align_angle_tol` 25, `orbit.settle_seconds` 1.5 (up to 3 s for bigger errors), `orbit.guide_segments` 96 (quality preset: Low 48 and no ticks).
- **Cost** (dev machine, `--profile`): the ring pass 0.009 ms (96 segments) / 0.007 ms (48), the HUD block about 0.013 ms of the UI pass; two draw calls (ring, ticks + markers), no per-frame allocation (buffers sized once; the block's strings refresh 10 times a second).
- **Seen edge-on:** every orbit through the ship contains the ship and the body centre, so from the cockpit (and nearly from chase view) the ring is always a line through the horizon, and looking straight at the body it crosses the body's middle; that is geometry, not a bug. The arc ahead, ticks and the two markers show which way to go; chase view from the side opens it up.
- **Playtest fixes (3.5d), root causes:** (a) "glitches out": the body's velocity was always 0 while unlocked (the previous position was overwritten in the same step it was read), so the "relative" velocity was the absolute one: a perfect orbit read "+12 m/s too fast" and the plane and heading carried the planet's 10-15 units/s drift; plus a 10-degree / 12 % window with no hysteresis flickered at the edge, and the settle snapped the radius (up to 175 m/s^2). (b) "trajectory in the centre": flying at the planet, the plane was set by the tiny (drift) tangential component, so the ring and arc ran through the centre; now the fallback plane. (c) "only shows after O": the guide was drawn before any key press (confirmed in saved games), but edge-on through the planet and amber, so it read as nothing; with the fixes it shows green-when-aligned and points sideways on a radial approach, within at least 2,500 units. Debug log: `guide on/off`, `guide plane: ...` changes.
- Code: pure rules `guide*` / `checkAlignment` / `settleStep` in `orbit_lock_rules.h`, tests in `package/tests/test_orbit_guide.cpp`.

## Engaging
Needs: ship alive, not warping, a body within `orbit.engage_range` radii above its surface (default 3) and at least `orbit.min_altitude` (20 units) above it, **and (default) aligned with the orbit, see above**. Otherwise it logs why and does nothing.
The ship is put on a circular orbit at its **current** altitude: speed `v = sqrt(mu / r)` (r = distance from the body centre), tangential, in the plane through the body centre containing the ship's current motion relative to the body
(so you keep going round the way you were going). If the motion is (nearly) straight at/away from the body, the plane is the guide's radial-approach fallback (nose, right vector, world axes; see above), the same one the guide shows.

**Gravity model** (a design, not real physics): `mu = orbit.gravity_scale * radius^2` (default scale 60), so bigger bodies pull harder. Examples: 400-unit planet at 1,000 units from its centre ~98 m/s;
the 1,800-unit sun at 5,400 ~188 m/s; a 50-unit moon at 150 ~31 m/s. One tunable, unit-tested.

## How the orbit is held
Each fixed step the ship's target position is `body position + start offset rotated about the orbit normal by omega * time_locked` (Rodrigues rotation of the offset taken at lock time, computed from scratch each step: no integration, **no drift**).
The body's next position is its current one plus its velocity (from the last two steps, kept for every body each step; a jump of a loaded game is ignored for that step). Since the ship integrates `position += velocity * dt`, the module sets `velocity = (target - position) / dt` through `IShip::setVelocity`.
So the ship follows the moving planet and the radius stays constant for as long as you like. Side effects: while locked the ship's world-frame speed includes the body's own orbital speed (a planet moves ~10-15 units/s),
and far from the origin (float positions, 350,000 units out) the speed readout jitters by a fraction of a m/s.

## Releasing
The ship keeps whatever velocity it has (Newtonian: it leaves along the tangent). It is released by: pressing the key again; **thrust, strafe, lift or brake** (`orbit.release_on_thrust`, default true; a dead zone of 0.1; brake counts because the orbit would silently override it);
any damage (collision, hit), death, respawn; or the warp drive engaging. Turning (pitch/yaw/roll) does not release it.
The lock is **not saved**: after loading a game the ship is simply flying with the velocity it had (an orbital velocity if it was locked), and you press O again.

## Event for the HUD
`ship::OrbitLockChanged{locked, bodyName}` (`orbit_lock_api.h`) on every engage/release, e.g. `ORBIT LOCKED: PLANET 1`. `bodyName` is also set on release. The HUD/cockpit is not touched by this module.

## Guide look: a stable ring and a colour gradient (w50)
- **Why the ring "recast"**: the ring used to be built with vertex 0 AT THE SHIP, so every frame all 96 vertices, the 30-degree ticks and the bright
  arc slid round the circle by the ship's travel (~1.5 units per frame at 90 m/s): the chords visibly crawled. Gravity (on by default) was the suspect,
  but measured while coasting under gravity (`--gravity-scenario=orbit`) the raw plane normal moves only ~1e-5 degrees per frame (rms; gravity pulls
  toward the body, i.e. inside the orbit plane, so it does not tilt it). Now the vertices and ticks sit at fixed angles from a fixed in-plane reference
  (`orbit::stableCirclePoints` / `planeReference`) and the bright arc is picked by angle ahead of the ship (`aheadAngle`): vertex motion dropped to
  ~0.45 units per frame, which is only the real change in orbit radius. The ring still passes through the ship.
- **Display smoothing of the plane**: `orbit::smoothNormal` eases the DRAWN plane toward the raw one (time constant 0.15 s) and takes any change over
  10 degrees at once, so noise (e.g. the nose-driven radial-approach plane) is damped and real turns never lag. The lock, the alignment check and
  the HUD numbers use the raw state, unchanged.
- **Colour**: `orbit::alignmentCloseness` (0 = perfect, 1 = at the tolerance edge; also used by `settleSecondsFor`) is evaluated against 3x the lock
  tolerances, and `orbit::guideColour` smoothsteps amber -> green over it; the ring, arc, ticks, markers and the HUD block's second line all share it.
  Aligned sits in the green third; far off (or outside the lock band) is full amber. Pressing O still uses the binary latched check.

## Tunables
`orbit.gravity_scale` 60, `orbit.engage_range` 3 (radii above the surface), `orbit.min_altitude` 20, `orbit.release_on_thrust` true.

## Dev flags
`--auto-orbit-lock=FRAME` and `--auto-orbit-lock-off=FRAME` act as if the key was pressed on that engine frame (toggles; ignored while paused). With `engine.log_level: debug` the module logs radius and relative speed once per second while locked.

## Difference from the old game
The old game's lock matched the planet's velocity (station-keeping) with a 2.5 s braking phase; this one is a real circular orbit around the body as designed for 3.5. Locking required speed <= 50 there; here any speed works.

## Code
Pure rules (no SDL/GL/engine): `orbit_lock_rules.h` (gravity, speeds, nearest body, range, plane fallback, position on orbit, release rule), tested in `package/tests/test_orbit_lock.cpp`.
