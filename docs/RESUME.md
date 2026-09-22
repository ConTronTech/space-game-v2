# Resume guide (updated 2026-09-22, after Leap 52)

## Where the project stands
`main` of `/home/contolis/Documents/Space-Game-V2/space-game-v2` is the real project (the old `space-game/space-game` folder is a read-only concept reference).
Newest verified tag: see `git tag -l 'good-*'` (latest `good-20260922-12`); every tag = green smoke + `make test-san` (529 tests, 45 modules).
The full history with what was verified and NOT verified is `docs/DEVLOG.md` (newest first). The roadmap is `docs/ROADMAP.md`; open questions for the user are `docs/QUESTIONS.md`; Phase 6's loop order is `docs/GAME_LOOPS.md`.

**Playable now:** Newtonian flight with real single-body sphere-of-influence GRAVITY (ON by default - a guide-speed orbit at Planet 1 holds altitude for 43+ simulated seconds), which now FADES smoothly near a
station you're actually approaching (`gravity.dock_assist_min` 0.1 at the dock zone) so planetary landings are no longer a fight against full surface gravity; cockpit / chase view with the real ShipV3 model and
a visible thruster nozzle; warp drive (5 km/s, upgrade levels); respawn; a seeded star system (sun, planets with terrain LOD meshes now reaching level 5/20,480 triangles at Ultra, moons) with a Fresnel-style
rim-glow ATMOSPHERE around planets (bright at the grazing limb/horizon, clear overhead, a faint screen tint while inside - replacing the old game's flat, distance-only halo); collisions; orbit lock with a
STABLE guide ring (no more per-frame "recasting" - fixed in-plane vertex anchoring) and a smooth amber-to-green alignment colour gradient instead of a hard snap; asteroid belts and clusters; orbital + flat
planetary stations with landing-pad docking; weapons (blaster, mining beam, missiles with auto-lock-on); particles; mining; per-ore cargo holds + a general hold; crafting from ore (requires docking by default),
gated behind BLUEPRINTS for advanced recipes (Missile Pack) - blueprints are found through exploring ANOMALIES (seeded sites, hidden until the `anomaly_scanner` perk, investigated for ore and/or a blueprint
unlock), never bought/sold; toast notification popups (`ui/toast`, anomaly/blueprint pickups are its first real callers); a system map (now also shows detected anomalies); radar with contacts (anomalies have a
height stem like bodies/stations); auto graphics quality; monitor awareness; a live PERFORMANCE profiler (F3/F4/F5) and a live STATE debugger (F6/F7/F8, `core/debugger`); joystick/wheel/pedals input with a
verified PXN V10 profile.
Design intent (user): hard, strategic, Newtonian; fuel/shield/repair come ONLY from mining + crafting (no free refills; `docking.test_refill` is test-only); same rules for future NPCs (none exist yet). Station
economy is explicitly WITHHELD by the user ("off the record", they want a specific design) - do not design/build it.
Standing rule (2026-09-21): workers/coordinator use `--display=1 --input-profile=testing` for any scripted/screenshot game run. Standing rule: new workers run **Opus 5 at LOW effort** and must report early (heartbeats).

## How work gets done (`docs/WORKFLOW.md`)
- Agents build in parallel worktrees and **never commit**; the coordinator reviews the diff, runs `package/tools/smoke.sh` + `make test-san` in the worktree, commits with `SG_COMMIT_APPROVED=1`, merges into `main`
  (`git merge --no-ff` is fine - `main` moves from coordinator doc commits between dispatches, a plain `--ff-only` will often fail and that's expected), rebuilds, reruns smoke + `make test-san` + a headless run on
  `main` itself, tags `good-YYYYMMDD-NN`, logs a DEVLOG entry.
- Dispatch: `orca-ide orchestration worker-start --run run_ac08f93c352e --spec "$(cat docs/agent_specs/.../wNN_*.txt)" --task-title "..." --worktree name:<worktree> --agent claude --model claude-opus-5 --effort low --display-name "..."`.
  Worktrees (all merged, idle as of Leap 52): `~/orca/workspaces/space-game-v2/{p2w1-ship-core,p2w1-cockpit,p2w1-ship-hud}` - fast-forward each with `git -C <wt> fetch <repo> main && git -C <wt> merge --ff-only FETCH_HEAD` before a new dispatch. Specs live in `docs/agent_specs/phase6/`.
- Run `run_ac08f93c352e` (bound to the coordinator terminal). Check messages with `orca-ide orchestration check --run run_ac08f93c352e --json`; a heartbeat needs no action, a `worker_done` needs the full review/merge cycle above, a `question` is answered with `orca-ide orchestration reply --run <run> --id <msg_id> --body "..."` (NOT `send --type question`).
- Worker scratch rule: scratch files only under `logs/` with literal names; never `rm` with a variable path (Claude Code blocks it and the worker stalls).
- Never inject keys/mouse into the user's desktop (no xdotool); use `--ui-click`, saved games, dev flags. Never dispatch into a worktree with an unprocessed report or uncommitted changes sitting in it (past collision, Leap 38). Do not edit the user's assets. Ask before anything hard to reverse.

## The test laptop ("crap-top") and the retro rig
`contolis@192.168.1.150`, key `~/.ssh/id_ed25519_spacegame_laptop`, game in `~/Documents/Space-Game-V2/space-game-v2` (mirrors the main rig; never copy game.json/settings.json). Intel HD (ILK), OpenGL 2.1, two displays (panel 1366x768 + a 1280x1024 CRT on VGA), PXN V10 wheel, SideWinder joystick, shifter + pedals later. Missing `libSDL2_ttf-2.0` was reported by the user on this machine (open item - not yet fixed/verified, see docs/QUESTIONS.md if tracked there or ask the user for the exact failure).
`package/tools/laptop_bench.sh [flags]` syncs the build and runs the benchmark there. Numbers before this session's Ultra/atmosphere changes: 70-75 fps on the panel (~59 on the CRT), 1% low ~23-29 - Low/Medium/High presets were explicitly kept byte-for-byte unchanged this session (only Ultra grew), and atmospheres are off at Low, so the laptop's numbers should be unaffected; not yet re-benchmarked to confirm.
Devices and profiles: `docs/DEVICES.md`, `docs/CONTROLLERS.md`. Leave nothing running there.

## Open items (see the newest DEVLOG entries)
**In flight:** gravity gradient debugger (w54, dispatched to `p2w1-ship-core` - extends the existing `gravity.draw` F6/F7 hook with concentric rings showing the field strength around the dominant body).
Deprioritized by the user (2026-09-21 night): stick shifter, wheel pause/back button, other accessory polish.
Withheld by the user, do not design: station economy (trading/prices/contracts).
Smaller/no-decision-needed follow-ups, not yet done: laptop's missing `libSDL2_ttf-2.0`; a free-fly debug camera; unifying `ship_hud`'s ad-hoc banners onto `ui/toast`; re-benchmarking the laptop after this session's Ultra/atmosphere work (expected unaffected, unverified).
Phase 6 loop items 1-2 (anomalies, blueprints) are done; see `docs/GAME_LOOPS.md` for the remaining ordered list (item 3+: ore tiers by zone, etc.) or the still-open combat depth/NPC design fork (no NPCs exist yet, weapons have nothing to fight - separate, larger decision, not blocking).

## Useful commands
`make -j8 && ./space_game_v2` · `make test-san` · `package/tools/smoke.sh` · `make joytest` (visual controller tester) · `./space_game_v2 --profile-overlay` (F3 in game) · `--list-displays` · `--list-joysticks` · `--benchmark=20 --no-vsync`.
