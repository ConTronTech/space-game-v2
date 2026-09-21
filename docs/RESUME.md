# Resume guide (updated 2026-09-21, after Leap 35)

## Where the project stands
`main` of `/home/contolis/Documents/Space-Game-V2/space-game-v2` is the real project (the old `space-game/space-game` folder is a read-only concept reference).
Newest verified tag: see `git tag -l 'good-*'` (latest `good-20260921-35`); every tag = green smoke + `make test-san` (452 tests, 38 modules).
The full history with what was verified and NOT verified is `docs/DEVLOG.md` (newest first). The roadmap is `docs/ROADMAP.md`; open questions for the user are `docs/QUESTIONS.md`.

**Playable now:** Newtonian flight (cockpit / chase view with the real ShipV3 model, thruster jets from the `@THRUST-JET` tag), warp drive (5 km/s, upgrade levels), respawn, a seeded star system
(sun, planets with terrain LOD meshes, moons), collisions (sun kills, scrape threshold), orbit lock with a visible alignment guide, asteroid belts and clusters, orbital + planetary stations with landing-pad docking,
weapons (blaster, mining beam, missiles with auto-lock), particles, mining (flashing capsules with a magnet), per-ore cargo holds + a general hold, crafting from ore (game menu `I`), radar with contacts,
auto graphics quality, monitor awareness (display / mode / aspect), live profiler (F3/F4/F5), joystick / wheel / pedals input with per-device profiles and an in-game controller setup.
Design intent (user): hard, strategic, Newtonian; fuel/shield/repair come ONLY from mining + crafting (no free refills; `docking.test_refill` is test-only); same rules for future NPCs.

## How work gets done (`docs/WORKFLOW.md`)
- Agents build in parallel worktrees and **never commit**; the coordinator reviews the diff, runs `package/tools/smoke.sh` + `make test-san`, commits with `SG_COMMIT_APPROVED=1` and merges (ff-only after merging main into the worktree branch), tags `good-YYYYMMDD-NN`, logs a DEVLOG entry.
- **New workers run Opus 5 at LOW effort** and must report early (heartbeats): `orca-ide orchestration worker-start --agent claude --model claude-opus-5 --effort low --task <id> --worktree id:4901aa59-f2a8-4d6b-a1f1-1f91fa5d2e8a::<worktree path>`.
  Worktrees (all merged, idle): `~/orca/workspaces/space-game-v2/{p2w1-ship-core,p2w1-cockpit,p2w1-ship-hud}` (fast-forward with `git -C <wt> merge --ff-only main` before a new task). Specs live in `docs/agent_specs/phase3_wave2/`.
- Run `run_ac08f93c352e` (bound to the coordinator terminal; a new session needs a NEW run). Acknowledge every message batch with `orca-ide orchestration check --run <run> --ack <delivery id>`. Use `orca-ide` (never bare `orca`).
- Worker scratch rule: scratch files only under `logs/` with literal names; never `rm` with a variable path (Claude Code blocks it and the worker stalls).
- Never inject keys/mouse into the user's desktop (no xdotool); use `--ui-click`, saved games, dev flags. Do not edit the user's assets (they are theirs). Ask before anything hard to reverse.

## The test laptop ("crap-top") and the retro rig
`contolis@192.168.1.150`, key `~/.ssh/id_ed25519_spacegame_laptop`, game in `~/Documents/Space-Game-V2/space-game-v2` (mirrors the main rig; never copy game.json/settings.json). Intel HD (ILK), OpenGL 2.1, two displays (panel 1366x768 + a 1280x1024 CRT on VGA), PXN V10 wheel, SideWinder joystick, shifter + pedals later.
`package/tools/laptop_bench.sh [flags]` syncs the build and runs the benchmark there (the game finds its `libs/` by rpath; `bundle_libs.sh` / `install_deps.sh` for other machines). Numbers: 70-75 fps on the panel (~59 on the CRT: more pixels), 1% low ~23-29. User quality bar: 60 fps standalone, no stutter.
Devices and profiles: `docs/DEVICES.md`, `docs/CONTROLLERS.md`. Leave nothing running there.

## Open items (see the newest DEVLOG entry)
Stick shifter (adapter pending), which wheel button = pause/back, ore scanner HUD effect, clean Esc handling (IInput "handled" flag), a `weapon_prev` action, the remaining laptop 1% low (~35 ms frames), Phase 5.2 toast / 5.6 system map, Phase 6 game loops (anomalies + scanner first), the gravity question (QUESTIONS.md).

## Useful commands
`make -j8 && ./space_game_v2` · `make test-san` · `package/tools/smoke.sh` · `make joytest` (visual controller tester) · `./space_game_v2 --profile-overlay` (F3 in game) · `--list-displays` · `--list-joysticks` · `--benchmark=20 --no-vsync`.
