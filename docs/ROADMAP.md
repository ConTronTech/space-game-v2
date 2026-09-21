# Space Game V2 - Roadmap

Rule for every step: **one module (or a few tiny ones), one commit, one "done when" you can see or test.**
Nothing gets a global variable. State lives inside a module and is shared through a **service**;
changes are announced with **events**. The old game's code is a reference only (read it, don't edit it).

Legend: `[x]` done, `[ ]` todo. Old-game source to port from is in *italics*.

---

## Phase 0 - Lock the base
- [x] Engine kernel, drop-in modules, event bus, services, pause
- [x] Core modules: window, input (JSON profiles, keyboard + mouse), render engine, glass UI, import
- [x] Pause menu, flight demo (6-axis, eased controls)
- [x] **0.1** First commit on `v2-modular`, tag `v2-base`
- [x] **0.2** `package/tools/smoke.sh`: clean `-Werror` build, module count, run 60 frames, screenshot. Run before every commit.
- [x] **0.3** Engine hardening: a module whose `init` throws is skipped instead of crashing; Makefile relinks when modules are added/removed; UI text is cached instead of rebuilt every frame; rule documented that `pressed()` belongs in `onUpdate`.
- [x] **0.4** Unit tests (`package/tests/`, `make test`, 28 tests): JSON parser, event bus, services, config, module load order / skipping / `--disable`, input edge detection. Part of `smoke.sh`.

## Phase 1 - Foundations (do these before any gameplay)
These stop the old game's problems (globals, hand-wired init, static flags) from coming back.

- [x] **1.0a Service interfaces**: `core::IInput` done (modules depend on the interface, not `InputHandler`). Every new service (audio, save, data, physics, camera) is created as an interface + implementation.
- [x] **1.0b logging**: `engine/log.h` - `LOG_I/W/E/D(tag, fmt, ...)` to the console and `logs/game.log` (level and file via `engine.log_level` / `engine.log_file`); all modules converted.
- [x] **1.0c Fixed-step interpolation** (`eng.alpha()`; flight blends prev/current state for the camera): render between physics steps so motion is smooth on 144 Hz displays (moved up from Phase 7).
- [x] **1.0d Assets**: V2 is self-contained. The old game's `assets/` (167 MB: models + skybox) was copied to `assets/` and is git-ignored (too big for git, same as the old repo). Worktrees symlink to the main checkout's copy (see docs/WORKFLOW.md).

- [x] **1.1 JSON writer** in `engine/json.h`: `Json::object().set(...)`, `array().push(...)`, `dump()`; floats save as `0.3` not `0.30000001`.
- [x] **1.2 `core/settings`** (`core::ISettings`): player prefs saved to `config/settings.json` (debounced, atomic write); owners apply changes via `SettingChanged`. FOV, fullscreen and mouse sensitivity are wired; pause menu Settings writes through it. Volumes come with audio (1.4).
- [x] **1.3 `core/save_system`** (`core::ISaveSystem` / `ISaveable`): modules save their own state; atomic writes; slot validation; format version; pause menu Save Game / Load Game. Flight is the first saveable. See docs/SAVES.md. Worlds regenerate from a saved seed. *savegame.h*
- [x] **1.4 `core/audio`** (`core::IAudio`, SDL_mixer): `play`, `playLoop`, master/effects/engine volumes from settings, sounds by name (files in `assets/sounds/` or built-in synthesized), no-op when there is no audio device. Engine hum + menu clicks + volume sliders wired. See docs/AUDIO.md. *systems/sound.h*  (NOTE: verified with SDL's dummy driver, not by ear)
- [x] **1.5 `core/data_registry`** (`core::IData`): content as JSON in `data/`; later files override earlier ones (mod-friendly); ores, items and recipes ported from the old tables; a test lints the shipped data. See docs/DATA.md. *ui/data/inventory.h, recipes.h*
- [x] **1.6 `core/camera`** (`ICamera`, `ITransformSource`): owns the view; the ship publishes a pose (`transform(alpha)`), flight no longer touches the camera. Cockpit and Chase modes (V, saved in settings; chase distance/height are tunables). Debug free-cam moves to the Phase 7 debug tools.
- [x] **1.7 `core/physics_world`** (`core::IPhysics`): spatial grid, swept-sphere tests (no tunnelling at warp speed), `Collided` events with normal/speed/contact positions, big-body list for planets. Flight demo: ship bounces off rocks with a thud and an IMPACT flash. See docs/PHYSICS.md. *physics.cpp*

Done when: settings persist across launches, a dummy module's state survives save/load, a test sound plays.  **Phase 1 complete.**

## Phase 2 - Ship
- [x] **2.1 `ship/ship_core`** (`ship::IShip`): the flight demo became the real ship - HP/regen, disposable shield, warp-fuel storage, hull plating cap, collision damage (old-game tiers), death, `kill`/`respawn`; events `DamageTaken` (hull + shield part), `ShieldBroken`, `FuelEmpty`, `Died`, `Respawned`; stats saved (old saves load; save id stays `gameplay/flight`). `ship.*` tunables. See docs/SHIP.md. *ship.h*
- [x] **2.2 `ship/warp_drive`**: `toggle_warp` (Z) engages a fuel-burning boost to 2000 m/s along the heading (accel 4000 m/s^2, 3.33 fuel/s), auto-disengage on empty/death, leaving warp cuts speed to `warp.exit_speed` (200, 0 = keep momentum), ignored while paused; `--auto-warp` dev flag. Verified incl. warping into a rock (fatal, drive shuts down). See docs/WARP.md. *ship.h*
- [x] **2.3 `ui/ship_hud`**: glass HUD - speed, HULL/SHLD/WARP bars, crosshair, warning banners, IMPACT total incl. shield, hit vignette, SHIP DESTROYED screen, fading controls hint. In cockpit view the flat overlay steps back because the data lives on the ship's screens (`hud.cockpit_overlay` = minimal | full | hidden, `hud.hint_seconds`). Dev provider `ship/fake_ship` (`--fake-ship=hp:35,shield:120,fuel:10,dead`). See docs/HUD.md. *hud.h*
- [x] **2.4 `ship/respawn`** (`ship::IRespawn`): after death waits `respawn.seconds` (3) then `IShip::respawn()`; HUD shows RESPAWNING IN N; `respawn.enabled=false` for hardcore. See docs/RESPAWN.md.
- [x] **2.5 `ship/cockpit`**: ShipV2 interior (default model, chosen by `ship.json` / `cockpit.ship`) drawn in view space in cockpit view. Custom `@GROUP-NAME` materials are extracted by the pure OBJ/MTL parser as tagged quads (not mesh); `@HUD-INFO/-SYSTEMS/-RADAR` show live FLIGHT_DATA / SHIP_SYSTEMS / PROXIMITY_RADAR from `IShip`. Other modules add screen content via `cockpit::ICockpitScreens`. See docs/COCKPIT.md. *cockpit.h, ship_loader.h*

**Phase 2 is complete** (waves 1 and 2 merged). There is still no way to gain shield/fuel in play (needs inventory, Phase 5).
Done when: you can fly, burn fuel, take damage, die, respawn and save/load mid-flight.

## Phase 3 - World
- [x] **3.0 Benchmark + hardware log** (done, see docs/BENCHMARK.md): startup log of GL renderer/version/max texture size; `--benchmark[=seconds]` prints frame-time avg / 1%-low / worst and exits, so the user can run it on the target laptop (docs/VISION.md, "Target hardware").
- [x] **3.1 `world/starfield` + `world/skybox`** (move the demo starfield out of flight). *starfield.h, skybox.h*  (skybox faces are 4-8 MB PNGs: cap/downscale them at load for the target laptop's shared graphics memory)
- [x] **3.2 `world/star_system`**: seeded sun, planets, moons, orbits, sun lighting. *starsystem.h*  (positions are 32-bit floats: render relative to the camera / re-origin the world so orbits of 80,000+ units and warp speeds don't jitter)
- [ ] **3.3 `world/planet_mesh`**: icosphere terrain, FBM noise, biomes, 4 LOD levels. *planet_mesh.h*
- [x] **3.4 Collision wiring**: sun, planets, moons register with `physics_world`; ship damage from speed; sun = death.
- [ ] **3.5 `ship/orbit_lock`**: one `toggle_orbit_lock` action. *ship.h*
- [ ] **3.6 `world/asteroids`**: belts, clusters, pooled LOD meshes. *asteroids.h*
- [ ] **3.7 `world/stations` + `ship/docking`**: orbital + planetary stations, dock/undock. *stations.h*  (**user instruction: keep docking SIMPLE - a cube and a cylinder as the platform placeholder until a proper model exists**; the old station OBJs in `assets/models/stations` can wait)

Done when: a generated system with planets, belts and stations that you can fly around, collide with and dock at.

## Phase 4 - Combat and mining
- [ ] **4.1 `fx/particles`**: engine exhaust, hit sparks. Modules emit `SpawnParticles`. *particles.h*
- [ ] **4.2 `combat/weapons`**: blaster, mining beam, missiles with lock-on. Each weapon is data + a small class, so new weapons are drop-ins. *weapons.h, weapons_update.h*
- [ ] **4.3 `gameplay/mining`**: destroyed asteroids drop ore to cargo (uses 5.1).

## Phase 5 - Inventory, crafting, menus
- [ ] **5.1 `gameplay/inventory`**: cargo capacity, stacks, ore types from `data/`. Saveable.
- [ ] **5.2 `ui/toast`**: info / warning / urgent popups, stackable. *ui_framework.h UIPopupManager*
- [ ] **5.3 UI tabs widget** in the UI handler (tab bar, list, grid, bar).
- [ ] **5.4 `ui/game_menu`**: tabbed menu (Map / Cargo / Crafting). Other modules add tabs by registering them, like panels. *game_menu.h, tabs/*
- [ ] **5.5 `gameplay/crafting`**: recipes from `data/`, item use. *recipes.h*
- [ ] **5.6 `ui/system_map`** tab. *map_tab.h*

Done when: mine an asteroid, see the ore in Cargo, craft something, use it.

## Phase 6 - Game loops (order from GAME_LOOPS.md)
Each one is a self-contained module that uses the services above and can be deleted without breaking the rest.

1. `gameplay/anomalies` + scanner - the "what's over there?" hook
2. `gameplay/blueprints` - unlocks that gate crafting
3. Ore tiers by zone (mostly data)
4. `gameplay/solar_flares`
5. Distress beacons + black boxes
6. Cargo pods
7. Ship modules (upgrades)
8. Derelict turrets
9. Solar wind + gravity assists
10. Mining drones
11. Environmental zones (radiation, nebula, magnetic)
12. Asteroid storms
13. Probe network
14. Comets

Survival pressure (fuel decay, hull wear, O2, heat) slots in at step 2.1 as small modules reading `ShipState`.

## Phase 7 - More ways to play
- [ ] `core/input_methods/joystick` (raw axes / buttons, deadzone, invert)
- [ ] `core/input_methods/gamepad` (SDL GameController, named axes)
- [ ] `core/input_methods/wheel` (PXN-V10 auto-calibration from the old game)
- [ ] `core/vr` (OpenVR loader, VR render target, VR menu panel) - *include/core/vr, ui/vr*
- [ ] Debug tools module: F-key cheats, god mode, free-cam, stats overlay (kept out of normal modules)
- [ ] Performance pass: batched draws, shader path if 2.1 fixed-function becomes the limit

---

## How to add a step cleanly
1. Read the old file(s) named in italics. Note the *behavior*, not the layout.
2. `package/tools/new_module.sh <category> <name>`; write only through services and events.
3. Put numbers in `data/` or `config/`, not in code, when a designer would want to tweak them.
4. Add actions to `config/input/default.json` instead of reading keys.
5. If it has state worth keeping, register it as saveable (after 1.3).
6. `package/tools/smoke.sh` (after 0.2), then commit: `feat(<module>): ...`.
7. Tick the box here.

---

## Parked ideas (later - NOT being built now)
Added 2026-09-20 at the user's request. Nothing here is scheduled; it only shapes small habits while the game is built.
Note: the old game's design doc listed multiplayer under "what this does NOT need"; the user has since decided to want it later.

| Idea | What it could mean | Keep this true while building now |
|---|---|---|
| **Local multiplayer** | Shared-machine play (split-screen / couch co-op) and/or LAN. Which one is undecided - ask before planning. | Game state lives inside modules and is saved through `ISaveable` (that is also what you would sync). The simulation runs on a fixed step (`onFixedUpdate`) so it can be made deterministic. Nothing assumes exactly one ship or one camera or one input source (`IInput` actions, `ICamera`, `IShip` are services, so a second instance is possible). |
| **Mobile app** (Android / iOS) | SDL2 runs on both, but the renderer is desktop OpenGL 2.1 fixed-function and mobile needs OpenGL ES 2, so drawing would need porting. Touch controls become another input-method module. | Input stays action-based (`config/input/*.json` + `InputMethod` modules), so touch is one new device. Draw code stays behind `RenderEngine` passes / `UIHandler`, not scattered. Keep game logic free of SDL/GL (pure headers with tests, as done so far). The 167 MB `assets/` skybox PNGs would need compressing. |
| **Browser build** (a "try the game" branch) | Emscripten / WebAssembly with WebGL. SDL2, SDL_mixer and SDL_ttf all have Emscripten ports. Fixed-function GL needs an emulation layer or a port. Assets must be small enough to download. | Avoid new Linux-only code outside `core/window` and `engine/main.cpp` (today `main.cpp` uses `/proc/self/exe`, which is Linux-only; note it). No threads, no raw file paths in modules (go through `ImportHandler` / config paths). Keep `assets/` sizes in mind. |

| **NPCs** (added 2026-09-20) | Ships with a bit of life. **They must obey the same physics and handicaps as the player** (no uncapped-speed awareness, same movement limits). Combat stays hard for both: dogfighting in space is realistically very difficult. | Keep the ship's physics/limits in `ship_core` reusable by more than one ship (no player-only hacks in the movement code); anything an NPC needs should go through the same `IShip`-style services. |
| **Drones** (added 2026-09-20) | Buildable drones that can be **programmed** (block programming and/or writing code) to do tasks, under the same space physics as everything else. **Power sources:** solar (early-mid; cannot use uranium; nearly infinite if kept in orbit around the sun or a planet, e.g. a beacon "pit stop" at a marked location), LiPo battery (mid game), uranium (late game). Builds on the Phase 6 "mining drones" idea; a later-game feature. | Keep power/energy as a data-driven concept later (`data/`), and keep automation behind services so a drone is "just another actor". Nothing to build now. |
| **Real space physics research** | See docs/VISION.md. Computationally expensive: optimisation is required. | Keep the fixed step, spatial grid and interpolation; measure frame time before adding heavy simulation. |

When any of these is picked up: start with a spike branch (`web/`, `mobile/`), not on `main`, and decide the renderer question (GL 2.1 vs GLES2/WebGL) first, since it decides the cost of both the mobile and browser builds.
