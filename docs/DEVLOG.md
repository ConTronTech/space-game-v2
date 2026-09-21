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

## Leap 3 - 2026-09-20: hardware log + `--benchmark` (built while Wave 2 runs)
- **Changed:** startup log of GL version/vendor/renderer/max texture, display driver, CPU threads and RAM (`core/window`); `--no-vsync`; new inert-unless-flagged
  `core/benchmark` module (`--benchmark[=seconds]`: avg fps, 1% low, worst frame, writes `logs/benchmark.txt`); `docs/BENCHMARK.md` (how the user runs it on the laptop);
  target-hardware rules in `docs/VISION.md`.
- **Verified:** 144 tests under ASan/UBSan (4 new for the statistics); the real game ran a 4 s benchmark (dev machine: RTX 2060 SUPER, ~2,260 fps, so not comparable to the laptop); a normal run prints no benchmark line.
- **Known:** no laptop numbers yet - the user needs to run `./space_game_v2 --benchmark=20 --no-vsync` on it and send `logs/benchmark.txt` and the first lines of `logs/game.log`.

## Leap 2 - 2026-09-20 (IN PROGRESS): Phase 2 Wave 2 - warp drive + respawn
- **Plan:** `ship/warp_drive` (2.2) by the ship-core worker; `ship/respawn` (2.4) + HUD "RESPAWNING IN N" by the HUD worker. Specs: `docs/agent_specs/phase2_wave2/`.
  Contract additions already on main: `IShip::setWarping` (non-pure), `ship::IRespawn`, input action `toggle_warp` (Z).
- **Design decisions to review when the user is back:** leaving warp cuts speed to `warp.exit_speed` (default 200 m/s, like the old game; 0 = keep momentum, pure Newtonian);
  warp key is Z (rebind in `config/input/default.json`); respawn takes `respawn.seconds` (default 3) and can be disabled with `respawn.enabled=false`.
- **State:** workers running; nothing merged yet. Last green tag: `good-20260920-01` (+ contract commit `527d965`, smoke pending).
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
