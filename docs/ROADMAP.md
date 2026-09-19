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
- [ ] **1.0d Assets decision**: V2 has no `assets/` and the old one is not in git. Choose copy / symlink / shared folder before Phase 2 needs the cockpit model and skybox.

- [x] **1.1 JSON writer** in `engine/json.h`: `Json::object().set(...)`, `array().push(...)`, `dump()`; floats save as `0.3` not `0.30000001`.
- [x] **1.2 `core/settings`** (`core::ISettings`): player prefs saved to `config/settings.json` (debounced, atomic write); owners apply changes via `SettingChanged`. FOV, fullscreen and mouse sensitivity are wired; pause menu Settings writes through it. Volumes come with audio (1.4).
- [x] **1.3 `core/save_system`** (`core::ISaveSystem` / `ISaveable`): modules save their own state; atomic writes; slot validation; format version; pause menu Save Game / Load Game. Flight is the first saveable. See docs/SAVES.md. Worlds regenerate from a saved seed. *savegame.h*
- [x] **1.4 `core/audio`** (`core::IAudio`, SDL_mixer): `play`, `playLoop`, master/effects/engine volumes from settings, sounds by name (files in `assets/sounds/` or built-in synthesized), no-op when there is no audio device. Engine hum + menu clicks + volume sliders wired. See docs/AUDIO.md. *systems/sound.h*  (NOTE: verified with SDL's dummy driver, not by ear)
- [x] **1.5 `core/data_registry`** (`core::IData`): content as JSON in `data/`; later files override earlier ones (mod-friendly); ores, items and recipes ported from the old tables; a test lints the shipped data. See docs/DATA.md. *ui/data/inventory.h, recipes.h*
- [ ] **1.6 `core/camera`**: owns the camera and modes (cockpit / chase / debug free-cam). Reads the ship transform from a service, so flight code never touches the camera.
- [ ] **1.7 `core/physics_world`**: simple spatial grid + collision events (`Collided{a, b, speed}`). Fixes the old "everything vs everything" checks. *physics.cpp*

Done when: settings persist across launches, a dummy module's state survives save/load, a test sound plays.

## Phase 2 - Ship
- [ ] **2.1 `ship/ship_core`**: turn the flight demo into the real ship. Transform, velocity, orientation, HP, shield, fuel. Publishes `ShipState` service; emits `DamageTaken`, `Died`, `Respawned`. Registers as saveable. *ship.h*
- [ ] **2.2 `ship/warp_drive`**: warp toggle, fuel drain, auto-disengage. One `toggle_warp` action serves keyboard, stick and wheel (the old game duplicated this code per device). *ship.h*
- [ ] **2.3 `ui/ship_hud`**: glass HUD - speed, HP / shield / fuel bars, crosshair, warnings via a toast system. *hud.h*
- [ ] **2.4 `ship/respawn`**: death timer, red overlay, state reset.
- [ ] **2.5 `ship/cockpit`**: load the OBJ cockpit through the import handler, drawn as a render pass. *cockpit.h, ship_loader.h*

Done when: you can fly, burn fuel, take damage, die, respawn and save/load mid-flight.

## Phase 3 - World
- [ ] **3.1 `world/starfield` + `world/skybox`** (move the demo starfield out of flight). *starfield.h, skybox.h*
- [ ] **3.2 `world/star_system`**: seeded sun, planets, moons, orbits, sun lighting. *starsystem.h*  (positions are 32-bit floats: render relative to the camera / re-origin the world so orbits of 80,000+ units and warp speeds don't jitter)
- [ ] **3.3 `world/planet_mesh`**: icosphere terrain, FBM noise, biomes, 4 LOD levels. *planet_mesh.h*
- [ ] **3.4 Collision wiring**: sun, planets, moons register with `physics_world`; ship damage from speed; sun = death.
- [ ] **3.5 `ship/orbit_lock`**: one `toggle_orbit_lock` action. *ship.h*
- [ ] **3.6 `world/asteroids`**: belts, clusters, pooled LOD meshes. *asteroids.h*
- [ ] **3.7 `world/stations` + `ship/docking`**: orbital + planetary stations, dock/undock. *stations.h*

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
