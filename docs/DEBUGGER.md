# In-game debugger (F6 / F7 / F8)

`core/debugger` is the GAMEPLAY/STATE twin of the live profiler (docs/PERFORMANCE.md): **F3-F5 = performance, F6-F8 = gameplay / physics state.**
It is always compiled in and costs nothing until F6 is pressed. Use it when the game does something weird while you play: look at the values,
switch on a debug draw, then press F8 and send back `logs/debug_state.txt`.

| Key | Action | What it does |
|---|---|---|
| F6 | `toggle_debugger` | opens / closes the panel (top left): tabs **Watches**, **Draws**, **Events** |
| F7 | `toggle_debug_draws` | shows / hides every debug draw that is switched on in the Draws tab (without closing the panel) |
| F8 | `dump_debug_state` | writes `logs/debug_state.txt` (works with the panel closed too; a short toast confirms it) |

- **Watches**: every live value a module registered, grouped by the text before the first `.` (`gravity.dominant` and `gravity.accel` show under `gravity`),
  re-read `debugger.watch_hz` times a second (4) ONLY while the panel is open. The built-in groups are `physics` (ship position, velocity, speed, the count
  of alive collision bodies via `core::IPhysics::aliveBodyCount()`; an `IPhysics` that does not override it - the default is `-1` - shows `n/a`), `ship`
  (hull, shield, warp fuel, alive/warping) and `engine` (time, frame, paused).
- **Draws**: one switch per registered draw hook (all off by default). The switches are clickable when the mouse is free (Tab). Built in: `physics.velocity`
  (magenta line from the ship along its velocity, 1 s ahead and at least 5 m; a short green line along the nose). Best seen in chase view (V).
- **Events**: the last `debugger.log_capacity` (200) game events, newest at the bottom; `up` / `down` buttons scroll (mouse free).
- A free-fly debug camera is **not** part of this first version (it needs its own camera mode): use V (cockpit / chase) with the panel. Follow-up.

## Tunables (`config/game.json`)
`debugger.enabled` (true), `debugger.watch_hz` (4), `debugger.log_capacity` (200).

## Dev flags (screenshots / tests without a keyboard)
`--debugger-open[=TAB]` (0 watches, 1 draws, 2 events), `--debug-draw=name[,name]` (switch hooks on at start), `--debug-f7=FRAME` (as if F7 were pressed
on that frame), `--debug-dump=FRAME` (as if F8).

## For module authors: plug in (optional service)
```cpp
#include "core/debugger/debugger_api.h"
// init():
if (auto* dbg = eng.services.get<core::IDebug>()) {
    dbg->watch("gravity.dominant", [this] { return dominantName_; });            // a std::string, built only while the panel is open
    dbg->drawHook("gravity.accel", [this](core::RenderEngine& r) { drawAccel(r); });
}
// when something happens (next to your own eng.events.emit):
if (auto* dbg = eng.services.get<core::IDebug>()) dbg->logEvent("gravity: dominant body -> " + dominantName_);
// shutdown():
if (auto* dbg = eng.services.get<core::IDebug>()) { dbg->unwatch("gravity.dominant"); dbg->removeDrawHook("gravity.accel"); }
```
- Name = `module.value`; the group is the part before the first `.`. Registering a name twice replaces the old getter / hook (a warning is logged).
- A getter that throws shows `<error: ...>` instead of crashing. Keep getters cheap and side-effect free.
- Add `core/debugger` to your `optionalDependencies()` so it inits first.

### What a draw hook may assume / must not change
The hooks run inside the debugger's render pass (`core/debugger`, order 190: after the world, before the cockpit at 800), only when switched on and F7 shows draws.
- **Given:** `GL_MODELVIEW` current and loaded with `r.camera.view`; projection set; lighting, texturing and face culling OFF; blending ON
  (`GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`); depth test ON, depth writes OFF; line width 1.5.
- **Must:** leave the matrix stacks as found (push/pop what you touch) and push/pop any other state you change. The debugger restores its own state
  (`glPushAttrib` / `glPopMatrix`) after all hooks, but hooks share it among themselves.
- **Far from the origin** (planets, the sun, anything in the star system) do what `ship/orbit_lock`'s guide does: take the camera position from the view
  matrix in double, zero the translation, `glLoadMatrixf` the rotation-only matrix (inside your own `glPushMatrix`/`glPopMatrix`) and send `point - camera`
  as float. `drawVelocity` in `debugger.cpp` is a 20-line example.

## Event log
`engine::EventBus` is typed only (`subscribe<E>` / `emit<E>`): there is no generic "every event" hook, and none was added. So the debugger subscribes
itself to the events that already exist (the headers are plain structs, subscribing costs nothing if the module is absent):
ship (`DamageTaken`, `Died`, `Respawned`, `ShieldBroken`, `FuelEmpty`), docking (`Docked`, `Undocked`), orbit lock (`OrbitLockChanged`, `OrbitLockRefused`),
combat (`WeaponChanged`, `Overheated`, `ProjectileHit`, `MissileExploded`), mining (`OreMined`), inventory (`CargoFull`), crafting (`CraftResult`, `ItemUsed`),
saves (`GameSaved`, `GameLoaded`), engine (`PauseChanged`). A NEW event type needs one `subscribe` line in `subscribeEvents()`, or the emitting module calls
`logEvent(...)` itself. Lines are `t=<game seconds>  <module>: <what>`. Physics `Collided` is deliberately not logged (dozens per second).

## The dump file (`logs/debug_state.txt`)
Self-explanatory, line based, parseable back (`dbg::parseDump` in `debugger_rules.h`):
```text
# SPACE GAME V2 - DEBUG STATE SNAPSHOT (written by F8, core/debugger; see docs/DEBUGGER.md)
# ... what this is / press F5 for performance ...
[header]
uptime: 3.24 s (frame 195)
...
[watches]
== physics ==
  ship_speed = 166.40 m/s
...
[draw hooks]
  physics.velocity: on
[event log]
  t=    14.49  combat: missile exploded (fuel and lifetime out)
```

## Cost (dev PC, NVIDIA, vsync off, 1280x720, 3 runs each)
Disabled 0.24 ms/frame, enabled + closed 0.24-0.26 ms (noise: nothing runs but three `pressed()` checks and two early returns), open on the Watches tab with
the velocity draw on 0.34-0.36 ms (+~0.1 ms, all in the UI text quads). Close it when not looking, like F3.

## Code
`package/modules/core/debugger/`: `debugger_api.h` (`core::IDebug`), `debugger_rules.h` (pure logic: watch registry, grouping, log ring, draw-hook toggles,
dump format; tests in `package/tests/test_debugger.cpp`), `debugger.cpp` (module, panel, pass, events, F8).
