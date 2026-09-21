# Dev log

Every "leap" (a chunk of verified work) gets an entry: what changed, how it was verified, the build state, known issues, what is next.
Newest first. `main` is only ever updated from a green state, and each verified state is tagged `good-YYYYMMDD-NN`, so there is always a
known-good point: `git checkout good-YYYYMMDD-NN` (or `git tag -l 'good-*'`). The binary `space_game_v2` in the repo root is rebuilt from `main`
only after the full check passes, so it should always run.

## Unattended-session rules (user asleep, 2026-09-20 onwards)
1. Never leave `main` or the root binary broken. Work happens in worktrees/branches; merge only after `package/tools/smoke.sh` passes.
2. After every merge: rebuild, run smoke + `make test-san`, run the game headless once, tag `good-*`, add a log entry here.
3. Agents never commit; every diff is reviewed before it is committed (see docs/WORKFLOW.md).
4. If quota runs out or something cannot be fixed cleanly: stop at the last green tag, write down the state in `docs/RESUME.md`, and do not start half-finished changes on `main`.
5. New ideas from the user go to `docs/ROADMAP.md` (parked ideas) / `docs/VISION.md`, not straight into code.

---

## Leap 16 - 2026-09-21 (DONE, tag `good-20260921-11`): Phase 3.7 stations + docking - PHASE 3 COMPLETE
- **Changed:** `world/stations` (seeded 1-3 stations, orbital or planetary, analytic positions, placeholder cube + cylinder models, static physics bodies, `world::IStations`) and `ship/docking` (`G` key: dock inside the zone (scale*60) below `docking.max_speed` 15 m/s, refusals logged with reasons, steered hold like orbit_lock, thrust or `G` undocks with a small push, events + `ship::IDocking`). `docking.test_refill` (default FALSE, test only: real fuel/shield come from mining). `stations.seed_offset` re-rolls stations only (default seed puts both on Planet 2 as surface stations).
- **Verified:** 235 tests under ASan+UBSan, smoke (29 modules), headless clean; saved-game scenarios: dock/hold (distance stayed 112.38 over 7 s while the station moved ~140 units)/undock, too fast, too far, planetary dock, station collision (other-tier damage + bounce), screenshots of both station types; benchmark unchanged.
- **Not verified:** the G key itself, test_refill path, docked state through death/warp (handlers only). **No HUD prompt or radar marker for stations yet** (the IDocking/IStations services are ready for it).
- **Next:** perf pass on the laptop (dispatch in flight), HUD "DOCK [G]" prompt + station/asteroid radar markers, then Phase 4.

## Leap 17 - 2026-09-21 (DONE, tag `good-20260921-12`): HUD dock prompt
- **Changed:** `ui/ship_hud` shows "STATION 1  420 m" near a station (`docking.prompt_range`, 0 = 4x dock radius), a green "DOCK [G]" when docking would work or a dim short reason ("too far", "too fast", "warp drive on", "orbit lock on"), "DOCKED: STATION 1 - [G] UNDOCK" while docked, DOCKED/UNDOCKED banners. Optional services only.
- **Verified:** 243 tests under ASan+UBSan, smoke (29 modules), headless clean; saved-game screenshots (too far, too fast, ready, docked; cockpit and chase views).
- **Not verified:** planetary stations on screen, a real flown approach; key label is a fixed "G" (IInput exposes no bindings).
- **Next:** perf pass (in flight), then station + asteroid radar markers (cockpit worker, after perf), then Phase 4.

## Leap 15 - 2026-09-21: laptop ("crap-top") online - first real numbers
- **Access:** SSH key `~/.ssh/id_ed25519_spacegame_laptop` -> `contolis@192.168.1.150` (Linux Mint 22.3, i5 M 560 2c/4t, HD Graphics ILK, OpenGL 2.1 Mesa 25.2.8, 7.6 GB, 1366x768). No sudo, and the SDL2 dev packages are NOT installed there, so nothing is built on the laptop: the main rig builds the generic binary and `package/tools/laptop_bench.sh` rsyncs it plus assets/data and the four missing runtime libs (SDL2_image/ttf/mixer, opusfile) into `~/space-game-v2-test/libs`. Its config/game.json and settings.json are never copied (laptop runs defaults). Nothing is left running.
- **Auto quality worked:** the laptop picked LOW by itself ("older Intel integrated graphics (Ironlake/Sandy Bridge class)").
- **Numbers (benchmark, no vsync, low preset, 1280x706):** 42.7 fps average, 1% low 14.9 fps, worst frame 67 ms. Target is 60 average / 30 floor: NOT met yet.
- **Where the time goes (average fps with parts disabled):** skybox off 52.0 (skybox costs ~4 ms/frame), cockpit off 58.4 (cockpit ~6.5 ms/frame), star system + asteroids off 41.5 (cheap), HUD off 41.9 (cheap), everything (skybox, star system, asteroids, starfield, cockpit) off 78.3 (the bare engine + UI + ship is still 12.8 ms/frame). The 1% low stays ~15 fps in EVERY variant (about 6 frames of ~65 ms in 640): a periodic hitch that is NOT any of those modules.
- **Next (perf task):** cockpit render cost, skybox fill cost, the base 12.8 ms, and the ~65 ms hitch. Target: 60 fps average on Low.

## Leap 12 - 2026-09-21 (DONE, tag `good-20260921-08`): radar height stems (user gripe 1)
- **Changed:** each radar contact keeps its flat position with a small base marker on the ship plane; its dot is offset up/down by a log-scaled height with a stem line (brighter above, dimmer below); label shows e.g. "PLANET 1  6.3K  UP 2.5K / DN 2.5K". Level (under 25 units or ~1 degree) shows no stem. Note: the old game radar in the reference tree has no stems, so this follows the user description.
- **Verified:** 217 tests under ASan+UBSan, full smoke, headless clean; screenshots above/below a planet from crafted saves.

## Leap 13 - 2026-09-21 (DONE, tag `good-20260921-09`): warp 5 km/s + upgrade levels (user gripe 2)
- **Changed:** `warp.speed` 5000 m/s, `warp.accel_factor` 0.4 (2.5 s to full speed), fuel drain unchanged (a tank carries ~144,000 units). New `data/warp_drive.json` with 4 levels (5 / 7.5 / 10 / 15 km/s, accel x1/1.2/1.5/2, fuel efficiency x1/1.25/1.6/2), `warp.upgrade_level`, `ship::IWarpDrive` service (level/levelCount/maxSpeed/setLevel/engaged) for Phase 5 upgrades, level saved as `ship/warp_drive`. Starfield streaks follow the drive top speed. Normal flight has NO speed cap (plain thrust reaches 5 km/s after ~125 s): unchanged.
- **Verified:** 221 tests under ASan+UBSan, smoke, headless clean; saved-game runs: full speed in 2.5 s, planet hit at 5003 m/s, sun hit, radius-3 asteroid hit, and at level 3 (15 km/s, 250 units/step) still no tunnelling.
- **Note:** old `warp.accel_factor` overrides of 2.0 in game.json should be removed. Level order comes from sorted data ids (use level00.. if there are ever more than 10).

## Leap 14 - 2026-09-21 (DONE, tag `good-20260921-10`): automatic graphics quality (user gripe 3)
- **Changed:** new `core/quality` (priority -900: after the window creates the GL context, before the world modules). Auto detects the preset from renderer/vendor/max texture/CPU threads/RAM/display (llvmpipe = Low, Intel integrated/Ironlake = Low/Medium, dedicated NVIDIA/AMD = High/Ultra, unknown = Medium); presets set 12 tunables (skybox size, star count, planet LOD/budget, asteroid counts/draw/budgets). Tiny engine change: `Config` gets a preset layer, precedence **game.json > preset > code default**. Settings menu row "Graphics: Auto (Ultra) (restart)" cycles Auto/Low/Medium/High/Ultra (saved as `quality.preset`, applies next launch), `--quality=` flag, benchmark prints the preset, Auto logs a warning (never changes anything) if fps stays under 25 for 10 s. Docs: `docs/QUALITY.md`.
- **Verified:** 229 tests under ASan+UBSan, smoke (27 modules), headless clean; this machine auto = ULTRA; `--quality=low` vs `ultra` logs differing values; a game.json value beats the preset; Settings row via --ui-click.
- **Not verified:** real Ironlake/AMD/Intel hardware (heuristic unit-tested with renderer strings only); that Low reaches 60 fps on the laptop; vsync is not tied to presets. Skybox High/Ultra only helps with source images above 1024 px.

## Leap 11 - 2026-09-21 (DONE, tag `good-20260921-07`): Phase 3.6 `world/asteroids`
- **Changed:** seeded static belts (band between two planet orbits) and clusters around planets/moons; struct-of-arrays field (default 1,740 rocks, 117 KB, generation 0.8 ms here), 4 shared lumpy meshes x 3 LOD levels + point batch for specks, camera-relative culled drawing, triangle budget, sun-lit; physics pool of at most 300 static "asteroid" bodies near the ship (add nearest-first, remove at 1.2x); read-only `world::IAsteroids` service (for mining/radar later). **The old demo rocks are now OFF by default (`flight.demo_rocks`)**, so the spawn area is empty; saved games referring to rock 0 need `flight.demo_rocks true`.
- **Verified:** 213 tests under ASan+UBSan, full smoke (26 modules), headless clean. Worker: belt screenshots (lit rocks, points in the distance), medium-rock collision 10 dmg at 100 m/s (right tier), warp at a rock = hit, no tunnelling; benchmark on/off within noise (~2400 fps); 21,500-rock stress test 1706 fps with the physics cap holding.
- **Not verified:** laptop; LOD popping; belt from far away; spin only seen in stills. Default belt is sparse (about 9 rocks within 1,500 units at the densest spot): raise `asteroids.belt_asteroids` for a denser one. Cluster asteroids do not follow their planet (static; drift over hours).
- **Next:** 3.7 stations + docking (cube + cylinder placeholder), radar contacts for asteroids via IAsteroids, then Phase 4 (particles, weapons, mining). Laptop benchmark still waits for SSH access.

## Leap 10 - 2026-09-21 (DONE, tag `good-20260921-06`): Phase 3.3 planet terrain meshes with LOD
- **Changed:** planets and moons are terrain meshes (icosphere levels 0-4, seeded FBM relief `world.terrain_height` 3%, biome colours from height/latitude, per-vertex normals), LOD from projected size with hysteresis and a triangle budget (`world.planet_triangle_budget` 20000, `world.planet_max_lod` 3, `planet_mesh.enabled`). Meshes are built lazily, at most ONE per frame, cached and freed after 30 s unused. Client vertex arrays (GL 2.1). Pure code in `planet_mesh.h`.
- **Verified:** 204 tests under ASan+UBSan, full smoke (25 modules), headless clean. Worker: level 4 builds in 1.1 ms here (est. 6-11 ms on the laptop), level 3 1,280 triangles / 31 KB; fps unchanged (~2200 with mesh on or off); screenshots near Planet 1 (continents, terminator), 15,000 units (dot), spawn (backdrop).
- **Not verified:** on the laptop; popping while flying through LOD changes; moons / Planet 6 close up; level 4 visually; more than 3 planets at once (budget path unit-tested only). Physics sphere stays at base radius (max mismatch 3% of radius).
- **Next:** 3.6 asteroids (belts, pooled LOD meshes), 3.7 stations (cube + cylinder docking placeholder), then Phase 4. Laptop benchmark still waits for SSH access.

## Leap 9 - 2026-09-21 (DONE, tag `good-20260921-05`): HUD shows the orbit lock
- **Changed:** `ui/ship_hud` subscribes to `OrbitLockChanged`: persistent "ORBIT LOCKED: <BODY>" status line (also shows above the cockpit screens in minimal mode) and a 2.5 s "ORBIT RELEASED" banner. Pure logic in hud_logic.h, works without ship/orbit_lock.
- **Verified:** 197 tests under ASan+UBSan, full smoke, headless clean; worker screenshots with a saved game 3000 units from the sun (locked at radius 2999.6, released banner).
- **Not verified:** a planet target on screen, fade timing visually.
- **Next:** 3.3 planet meshes with LOD (biggest visual step; budget for the laptop), 3.6 asteroids, 3.7 stations (cube + cylinder), then Phase 4. Laptop benchmark still waits for SSH access.

## Leap 8 - 2026-09-21 (DONE, tag `good-20260921-04`): Phase 3.5 `ship/orbit_lock`
- **Changed:** `O` (`toggle_orbit_lock`) locks the ship onto a circular orbit around the body with the nearest surface (within 3 radii, min altitude 20), speed sqrt(mu/r), mu = `orbit.gravity_scale` (60) x radius^2. Position is held analytically each step through `IShip::setVelocity` only (no ship_core change) and follows the moving body. Thrust/strafe/lift/brake (tunable), damage, death, respawn and warp release it. Event `ship::OrbitLockChanged` for the HUD (not shown yet). Not saved. Docs: `docs/ORBIT_LOCK.md`.
- **Verified:** 193 tests under ASan+UBSan, full smoke (25 modules), headless run clean. Real-game saved scenario 1000 units above Planet 1: locked at radius 1410.2, circular speed 84.2 m/s, radius drift ~0.001 over 13 s while the planet moved 160 units; released by toggle; lock refused at spawn (sun too far).
- **Not verified:** release by thrust/damage/death/warp in the running game (unit-tested / handlers written only), locking to a moon or the sun, the O key itself, far-from-origin float jitter.
- **Next:** HUD line "ORBIT LOCKED: <body>" (consume the event), 3.3 planet meshes with LOD, 3.6 asteroids, 3.7 stations.

## Leap 7 - 2026-09-21 (DONE, tag `good-20260921-03`): radar contacts (cockpit PROXIMITY_RADAR)
- **Changed:** the cockpit radar plots the sun, planets and moons from `IStarSystem` (double subtraction, ship frame, log-scaled range, `cockpit.radar_range` 400000, nearest 20, label "PLANET 1  1.5K"). Pure maths in `radar_map.h`. No star system = empty scope as before.
- **Verified:** 186 tests under ASan+UBSan, full smoke, headless run clean; worker screenshots at spawn (sun dot + "SUN 10.2K") and near Planet 1.
- **Note:** the worker used xdotool to click the pause menu for a screenshot (I had asked for no desktop input injection; it also got stuck on an `rm -rf $S/*` prompt that I declined). Idea: add a `--load=<slot>` dev flag so saved-game screenshots need no clicking (`--ui-click` already exists and should be used).
- **Not verified:** radar while flying/rotating, moons up close; demo rocks are not on the radar (no service exposes them).
- **Next:** 3.5 orbit lock, 3.3 planet meshes with LOD, then 3.6 asteroids, 3.7 stations.

## Leap 6 - 2026-09-21 (DONE, tag `good-20260921-02`): Phase 3.4 collision wiring
- **Changed:** sun/planets/moons are static physics spheres that follow their orbits (setBody with velocity, so closing speed is relative to the moving body). Planet/moon hits use the tiered speed damage + bounce; sun = instant death (`ship.sun_kills`, default true). Bounce now uses the reported closing speed so an orbiting planet cannot swallow a parked ship.
- **Verified:** 180 tests under ASan+UBSan, full smoke (24 modules), headless run clean. Worker scenarios: 100 m/s into a planet = 25.3 dmg + bounce; sun = death + respawn at spawn; warp (2000 m/s) at a planet/sun = hit, no tunnelling; benchmark unchanged.
- **Known:** float physics positions at 350,000 units have ~0.03 ulp and a few units of contact error in the swept test (fine for planets; later: double-precision physics or ship-relative rebasing). Moons not hit in the real game (same code path). Radar not wired yet (follow-up: consume IStarSystem::bodies()).
- **Next:** 3.5 orbit lock, 3.3 planet meshes with LOD, radar contacts from the star system, then 3.6 asteroids, 3.7 stations.

## Leap 5 - 2026-09-21 (DONE, tag `good-20260921-01`): Phase 3.2 `world/star_system`
- **Changed:** seeded system (seed 1234: sun + 6 planets + 8 moons = 15 bodies, outer orbit 352,200 units), analytic circular orbits in double, camera-relative rendering with far-body clamping, low-poly lit spheres, emissive sun + glow, saveable (seed + sim time), `IStarSystem` service. Tunables: `star_system.enabled`, `world.seed`, `planets.count`, `world.sun_distance`, `world.time_scale`, `world.sphere_detail`.
- **Verified:** 177 tests under ASan+UBSan, full smoke (24 modules), headless run; benchmark on/off within noise (~2200 fps). Screenshots checked by the worker (sun disc, lit planet, far dot).
- **Not verified / for the user:** looks on the laptop, moving orbits (periods are hours; try `world.time_scale` 200), moons, no collisions yet (3.4). Sun is at ~12,000 units from the spawn (behind-left, HDG ~248).
- **Next:** 3.3 planet meshes with LOD, or 3.4 collision wiring (sun = death) - 3.4 first is cheaper and makes the system solid.

## Leap 4 - 2026-09-20 (DONE): Phase 3.1 starfield + skybox for the low-end laptop

**Final state (tag `good-20260920-06`):** skybox top/bottom seam bug fixed (implicit flipV on faces 4 and 5); default set seam ratio top 3.57 -> 0.95, bottom 2.78 -> 1.33. `tools/skybox_seams.py` default threshold 2.5. 171 tests green under ASan+UBSan, full smoke OK (23 modules). Next: Phase 3.2 star system.
- **Plan:** `world/starfield` (moved out of ship_core, with warp streaks) and `world/skybox` (faces downscaled to `skybox.max_size` 1024, loaded one at a time, downscale cache in `cache/skybox/`).
  Spec: `docs/agent_specs/phase3_wave1/w6_starfield_skybox.txt`. Worker: the ship-core/warp worker (dispatch `ctx_ea162b7dcbab`). Reports skybox on/off benchmark difference.
- **State (updated on a wake-up):** starfield + skybox are MERGED into main (170 tests under sanitizers; code reviewed; big-face path verified: a 4096 px / 7.9 MB face set loads in 3.0 s cold, 0.27 s from cache, 18 MB of textures).
  **Known visual bug, fix in flight:** the skybox top/bottom faces are vertically inverted vs the old-game convention (default set dark/set1 pole seams: top 3.57, bottom 2.78 where 1.0 = seamless;
  every set with a skybox.json gets worse with its flip_v). Measured with the new `package/tools/skybox_seams.py`. FIX LIST #1 (implicit flipV on top/bottom + tests + verify with the tool) is with the worker
  (dispatch `ctx_a7063b093deb`). Side seams are fine. Full smoke on merged main is running; tag `good-20260920-05` follows it, then the fix merge gets its own tag.
  Note for the laptop: a cold first launch with a large skybox set decodes big PNGs once (3 s here, several times slower on the laptop); the default set is 1024 px and needs no downscale.

## Leap 3 - 2026-09-20: hardware log + `--benchmark` (built while Wave 2 runs)
- **Changed:** startup log of GL version/vendor/renderer/max texture, display driver, CPU threads and RAM (`core/window`); `--no-vsync`; new inert-unless-flagged
  `core/benchmark` module (`--benchmark[=seconds]`: avg fps, 1% low, worst frame, writes `logs/benchmark.txt`); `docs/BENCHMARK.md` (how the user runs it on the laptop);
  target-hardware rules in `docs/VISION.md`.
- **Verified:** 144 tests under ASan/UBSan (4 new for the statistics); the real game ran a 4 s benchmark (dev machine: RTX 2060 SUPER, ~2,260 fps, so not comparable to the laptop); a normal run prints no benchmark line.
- **Plan (user, 2026-09-20 night): focus on the main rig first; the user will give SSH access to the laptop in the morning.** Then: check the laptop has the build dependencies (SDL2 dev libs), build a *generic* binary there
  (no -march=native), run `./space_game_v2 --benchmark=20 --no-vsync`, read `logs/game.log` for the real GL version, and record it in this log. Use only what is needed over SSH.
- **Known:** no laptop numbers yet - the user needs to run `./space_game_v2 --benchmark=20 --no-vsync` on it and send `logs/benchmark.txt` and the first lines of `logs/game.log`.

## Leap 2 - 2026-09-20 (DONE, tagged good-20260920-03/04): Phase 2 Wave 2 - warp drive + respawn
- **Plan:** `ship/warp_drive` (2.2) by the ship-core worker; `ship/respawn` (2.4) + HUD "RESPAWNING IN N" by the HUD worker. Specs: `docs/agent_specs/phase2_wave2/`.
  Contract additions already on main: `IShip::setWarping` (non-pure), `ship::IRespawn`, input action `toggle_warp` (Z).
- **Design decisions to review when the user is back:** leaving warp cuts speed to `warp.exit_speed` (default 200 m/s, like the old game; 0 = keep momentum, pure Newtonian);
  warp key is Z (rebind in `config/input/default.json`); respawn takes `respawn.seconds` (default 3) and can be disabled with `respawn.enabled=false`.
- **State:** respawn (2.4) is MERGED and verified in the real game (lethal hit -> SHIP DESTROYED + RESPAWNING IN 3 -> alive at spawn, HULL 100/100); 154 tests under sanitizers.
  Warp drive (2.2) merged after one fix list (ignore Z while paused; do not cache the IShip pointer). Real-game checks by the coordinator: engage (SPD 2000, DRIVE ENGAGED, fuel 97), run dry (drops out, WARP FUEL EMPTY),
  warp into a rock from 300 m (fatal, swept collision catches it, drive disengages), menu open (does not engage). 163 tests under sanitizers; full smoke green (21 modules). Tags `good-20260920-03` (respawn) and `good-20260920-04` (warp).
  Known: `--fake-ship` does not show DRIVE ENGAGED (fake_ship has no setWarping override; dev-tool only). Also merged: Qwen-written QUICKSTART refresh.
- **Next after this leap (updated: the user's laptop specs are now in docs/VISION.md):** FIRST 3.0 hardware log + `--benchmark` mode; then 3.1 starfield + skybox out of `ship_core`
  as a world module (with a texture-size cap for the laptop), then 3.2 star system. Qwen can take small doc/data/test jobs (max 2 at once).

## Leap 1 - 2026-09-20: Phase 2 Wave 1 merged, cockpit + HUD-on-the-ship is the default
- **Changed:** `ship/ship_core` (real ship: HP, regen, shield, fuel storage, collision damage, death, `IShip`), `ui/ship_hud` (glass HUD; steps back in
  cockpit view; IMPACT shows shield-inclusive total; hint fades), `ship/cockpit` (ShipV2 interior, generic `@GROUP-NAME` materials extracted as tagged quads,
  live INFO/SYSTEMS/RADAR screens from `IShip`, `ICockpitScreens` service), `ship/fake_ship` dev provider (`--fake-ship=...`, now really overrides the real ship).
  Also: pause-menu **Load Game crash fixed** (a click changed the page mid-draw), `--ui-click` test hook, agents-never-commit hook, config/settings/save/audio/data/camera/physics
  services from Phase 1.
- **Verified:** 140 unit tests under ASan/UBSan; strict `-Werror` smoke on `main` (18 modules); scripted real-game scenarios with screenshots: default cockpit view, shielded
  hit (`IMPACT 10 ASTEROID (SHIELD)`, exact damage 10.000 at 100 m/s), lethal hit -> SHIP DESTROYED + `STATUS DESTROYED` on the screens, chase view, `--fake-ship`.
- **Build:** `main` at the merge of `p2w1-ship-hud` (fake_ship fix) plus docs; root binary rebuilt.
- **Known gaps:** no way to gain shield/fuel in play yet (inventory is Phase 5); death ends on a frozen screen until respawn (2.4); radar has no contacts until the world exists;
  audio only verified with the dummy driver; flight feel and mouse-look not hand-tested by the coordinator.
- **Next:** Wave 2 - warp drive (2.2) and respawn (2.4).
