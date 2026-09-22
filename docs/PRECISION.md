# Double-precision ship/physics positions (7.1)

Foundational fix, built as a prerequisite for rescaling the star system to real astronomical distances (docs/ROADMAP.md,
user request 2026-09-22: "the scale of the game is too small... a red giant has millions of km between its planets").

## The problem

Real astronomical distances run from tens of millions of metres (a Mercury-scale orbit) to billions (an outer planet's
orbit). `engine::Vec3` (the type the ship's own position, and every `core::IPhysics` body position, used to be) is
**float32**, which has about 7 significant decimal digits. At 5e9 units, one float step (one ULP) is already **512
metres** - the ship's own reported position would visibly jitter/snap in hundred-metre increments, and any physics math
that combines two nearby-but-huge float coordinates loses essentially all of the sub-metre precision that flight,
docking, combat and collision all depend on.

`world::Vec3d` (double) already existed for planet/asteroid/station positions and was rendered correctly via the
existing "camera-relative" convention (subtract in double, cast only the small remainder to float for GL - see
`world/star_system`, `world/atmosphere`, `world/asteroids`). The gap was that the **ship's own authoritative position**
and **physics body positions** were not part of that discipline, so they poisoned every calculation that combined them
with a body's already-double position: `ship/gravity`'s dominant-body search, `ship/orbit_lock`'s
`(target - position) / dt`, `ship/docking`'s pad-hold math, and `core/physics_world`'s collision sweep test.

## The fix

Extend the existing double-position convention to the ship's own position and to physics, rather than inventing a
floating-origin/re-centering scheme:

- `engine::Vec3d` (a plain double aggregate) lives in `package/engine/math.h` now, promoted out of
  `world/star_system/star_system_api.h` (core modules - physics, the camera pose - need it too, and `core/` must not
  depend on `world/`). `world::Vec3d` stays as a 1-line alias, so every existing `world`/`ship` module call site keeps
  compiling unchanged.
- `ship_core`'s internal position (`pos_`) is now `Vec3d`, integrated in double each fixed step
  (`ship::rules::integrate`: `pos += (double)vel * dt`). Velocity stays float (m/s-scale values lose nothing at any
  distance; only *position*, which accumulates every step, needed the precision).
- `ship::IShip` gained `positionD()` / `setPoseD()` - **additive**, non-pure, default-widening/narrowing so every
  existing caller of the float `position()`/`setPose()` keeps compiling. The float accessor is now an intentional
  *approximation*: fine for anything cosmetic or already-relative-and-small (HUD readouts, radar, particle spawn
  points), wrong for anything that combines the ship's position with another absolute double position.
- `core::IPhysics` gained additive double overloads of `addBody`/`setBody`/`teleport`/`query`, implemented as SFINAE
  templates: a braced `{x, y, z}` argument can never deduce a template parameter, so it always resolves to the double
  virtual method (never ambiguous), while a caller holding a concrete `engine::Vec3` picks the float template overload
  that widens to double. The float signatures are the old ones; nothing outside this task's ownership needed to
  change (`world/star_system`'s own `IPhysics` calls still compile as-is). `core::PhysicsWorld` stores body positions
  as `Vec3d` and runs the swept contact test entirely in double - a sphere-sphere contact needs sub-metre accuracy in
  the *difference* of two positions, and far from the origin a float difference alone was already hundreds of metres
  off before any test could run.
- `ship/gravity`, `ship/orbit_lock`, `ship/docking` now call `positionD()` directly wherever they used to widen a
  lossy float `position()` up to `world::Vec3d` - this actually *simplifies* those call sites (removes a round-trip),
  not just fixes them.

## What was deliberately left out (a tracked follow-up, not forgotten)

The **render/camera chain** is still float end-to-end past this fix: `core::Pose` gained an additive `posD` field
(ship_core fills it, `cam::chasePose` carries it through), but `cam::viewMatrix()` still stores the eye as
`-dot(axis, pose.pos)` in float, and every camera-relative render pass (`world/star_system`, `world/atmosphere`,
`world/stations`, `world/asteroids`, `ship/gravity`'s debug draw) still *recovers* the eye by reading it back out of
that float view matrix. A dedicated `--camera-jitter-log=N` diagnostic (`core/camera`) measures exactly how far that
recovered eye drifts from the true double eye position frame to frame - see the numbers below. A full fix (the render
eye carried as double end-to-end, every draw pass genuinely camera-relative) is intentionally a separate, dedicated
task, since it touches a large number of draw passes and deserves its own focused review rather than being folded into
this one.

## Measured proof

`logs/w56_measure.txt` (scenario details in that file). Ship position/physics precision, double vs. the old float,
at increasing distance from the origin:

| Distance | Metric | Double | Old float |
|---|---|---|---|
| today (~4.1e4) | gravity orbit spread / eccentricity (300 s) | 1.67 / 8.16e-4 | 1.74 / 8.55e-4 (no meaningful difference) |
| 1e7 | gravity orbit spread / eccentricity | 1.67 / 8.16e-4 | 171.5 / 7.79e-2 (orbit visibly degrading) |
| 5e9 | gravity orbit spread / eccentricity | 1.67 / 8.17e-4 | 0.0 / 0.0 (collapsed - the ship effectively stopped moving relative to the body) |
| today | orbit lock worst radius/speed error | 0.120 m / 1.05 m/s | 0.120 m / 1.10 m/s |
| 1e7 | orbit lock worst radius/speed error | 0.120 m / 1.05 m/s | 0.539 m / 30.6 m/s |
| 5e9 | orbit lock worst radius/speed error | 0.120 m / 1.05 m/s | 256.4 m / 15,579 m/s |
| today | dock hold worst miss/judder | 0.0 m / 0.0 m | 8.4e-4 m / 1.2e-3 m |
| 1e7 | dock hold worst miss/judder | 0.0 m / 0.0 m | 0.274 m / 0.200 m |
| 5e9 | dock hold worst miss/judder | 0.0 m / 0.0 m | 206.2 m / 127.8 m |

Camera-jitter diagnostic (render chain, still float - the tracked follow-up above), recovered-eye error vs. the true
double eye:

| Distance | Worst error | Frame-to-frame jitter (worst / mean) |
|---|---|---|
| origin | 3.1e-5 m | 4.1e-5 m / 1.8e-5 m |
| 1e7 | 0.264 m | 0.167 m / 0.111 m |
| 1e8 | 3.37 m | 2.33 m / 1.94 m |
| 5e9 | 216.1 m | 126.3 m / 3.33 m |

## Testing

`--ship-start=X,Y,Z` places the ship (and its spawn point) at an arbitrary double position, for testing precision far
from the origin without needing a rescaled system. `--precision-log=N` logs N fixed steps' worth of position-delta
error, running a float "mirror" of the old integration alongside the real double one from the same starting point, so
one run shows the before/after side by side. `--camera-jitter-log=N` is the render-chain diagnostic above. See
`package/tests/test_precision.cpp` for the full regression + large-scale test suite.

## What did NOT change

Nothing about today's small-scale gameplay is different - this is pure precision plumbing. Every existing test passed
unchanged; the regression numbers above show double and float are indistinguishable at today's distances. The actual
rescale of sun/planet/orbit distances, warp-speed retuning and asteroid-belt draw-distance scaling are separate,
later tasks that can now build on this safely.
