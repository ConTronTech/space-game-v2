# Resume guide (updated 2026-09-22, after Leap 43)

## Where the project stands
`main` of `/home/contolis/Documents/Space-Game-V2/space-game-v2` is the real project (the old `space-game/space-game` folder is a read-only concept reference).
Newest verified tag: see `git tag -l 'good-*'` (latest `good-20260922-03`); every tag = green smoke + `make test-san` (492 tests, 42 modules).
The full history with what was verified and NOT verified is `docs/DEVLOG.md` (newest first). The roadmap is `docs/ROADMAP.md`; open questions for the user are `docs/QUESTIONS.md`.

**Playable now:** Newtonian flight with real single-body sphere-of-influence GRAVITY (ON by default - a guide-speed orbit at Planet 1 holds altitude for 43+ simulated seconds), cockpit / chase view with the
real ShipV3 model and a visible thruster nozzle (the `@THRUST-JET` tag is BOTH a particle emitter AND drawn as ordinary hull geometry, using the tag material's own colour), warp drive (5 km/s, upgrade levels),
respawn, a seeded star system (sun, planets with terrain LOD meshes, moons), collisions (sun kills, a scrape threshold + contact cooldown so holding thrust against a surface no longer drains HP), orbit lock with
a visible, forgiving (25%/25deg) alignment guide, asteroid belts and clusters, orbital + FLAT planetary stations (no pedestal, grounded on the real terrain) with landing-pad docking, weapons (blaster, mining beam,
missiles with auto-lock-on and an ore scanner effect), particles, mining (flashing capsules with a magnet), per-ore cargo holds + a general hold, crafting from ore (game menu `I`: CARGO / CRAFTING / MAP tabs,
crafting now REQUIRES docking by default), toast notification popups (`ui/toast`, nothing wired to it yet), a system map, radar with contacts, auto graphics quality, monitor awareness (display / mode / aspect),
a live PERFORMANCE profiler (F3/F4/F5) and a live STATE debugger (F6/F7/F8, `core/debugger`, other modules opt in), joystick / wheel / pedals input with a verified PXN V10 profile (steering axis 0, pedals rest
at min, one weapon-switch button, D-pad -> menu navigation) and an in-game controller setup screen.
Design intent (user): hard, strategic, Newtonian; fuel/shield/repair come ONLY from mining + crafting (no free refills; `docking.test_refill` is test-only); same rules for future NPCs (none exist yet).
Standing rule (2026-09-21): workers/coordinator use `--display=1 --input-profile=testing` for any scripted/screenshot game run, so the desktop mouse/controller on the user's primary display is never touched.

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
Deprioritized by the user (2026-09-21 night, "the main game needs focusing"): stick shifter (adapter pending), wheel pause/back button, other accessory polish.
Design decisions genuinely needed from the user before building: Phase 6's first game loop item (`gameplay/anomalies` + scanner - no `docs/GAME_LOOPS.md` exists) vs combat depth (no NPCs exist, so weapons/missiles have
nothing to fight) - **this is the main open fork**; a station economy is planned by the user but withheld ("off the record") until their own spec arrives - do not design it. Smaller/no-decision-needed: a
no free-fly debug camera, unifying ship_hud's ad-hoc banners onto `ui/toast` (flagged, not done - too invasive to do unattended).

## Useful commands
`make -j8 && ./space_game_v2` · `make test-san` · `package/tools/smoke.sh` · `make joytest` (visual controller tester) · `./space_game_v2 --profile-overlay` (F3 in game) · `--list-displays` · `--list-joysticks` · `--benchmark=20 --no-vsync`.
