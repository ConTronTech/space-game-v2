# Resume guide (updated 2026-09-22, after Leap 58)

## Where the project stands
`main` of this repo (`~/space-game-v2` or wherever you cloned it) is the real project (an earlier concept prototype was used as a read-only reference during early development, not included here).
Newest verified tag: see `git tag -l 'good-*'` (latest `good-20260922-18`); every tag = green smoke + `make test-san` (564 tests, 46 modules).
The full history with what was verified and NOT verified is `docs/DEVLOG.md` (newest first). The roadmap is `docs/ROADMAP.md`; open questions for the user are `docs/QUESTIONS.md`; Phase 6's loop order is `docs/GAME_LOOPS.md`.

**Session paused here at the user's request (2026-09-22) for a manual review** of the game's current state and any bugs, without dispatching agents. Do not start new worker dispatches until the user says to resume.

**Playable now:** everything from the Leap 52 summary (gravity + dock-assist fade, atmosphere, Ultra planet detail, orbit guide stability, blueprints/anomalies, ore tiers by zone, gravity gradient debugger, skybox
matches the seeded star's colour), PLUS:
- **A real freeze was diagnosed and fixed** (Leap 56): the skybox star-colour matching feature decoded every skybox image (up to 4096x4096) synchronously at startup just to compute an average colour - now
  shipped as precomputed data (`data/skybox_colors.json`), zero extra decode cost. The deeper gap (loadModules() ran every module's init() with no SDL event pump, letting the OS flag the app as
  unresponsive) is also fixed generically.
- **A boot loading screen** (Leaps 56-57): `core/boot_screen` shows a real progress bar plus the current module name and a scrolling log of recently-loaded modules, guaranteed to draw on top of everything
  else via a new `engine::Module::onBootOverlay` hook (a safety mechanism, not cosmetic).
- **Double-precision ship/physics positions** (Leap 58, docs/PRECISION.md): foundational fix for the user's realistic-scale rescale request. The ship's own position and physics body positions are now
  double (`positionD()`/`setPoseD()` on `IShip`, additive double overloads on `IPhysics`) instead of float, which broke down catastrophically far from the origin (512 m per step at 5e9 units). Measured
  proof in the doc. The render/camera chain is a DELIBERATE, tracked follow-up (still float end-to-end; `--camera-jitter-log` measures it) - not yet built.
- **Saves and cache were cleared** (user request, 2026-09-22) for a fresh start - `saves/` and `cache/` are both empty; they repopulate automatically on next launch/save.

Design intent (user): hard, strategic, Newtonian; fuel/shield/repair come ONLY from mining + crafting (no free refills; `docking.test_refill` is test-only); same rules for future NPCs (none exist yet). Station
economy is explicitly WITHHELD by the user ("off the record", they want a specific design) - do not design/build it.
Standing rule (2026-09-21): workers/coordinator use `--display=1 --input-profile=testing` for any scripted/screenshot game run. Standing rule: new workers run **Opus 5 at LOW effort** (HIGH effort for
foundational/risky architecture work like the precision fix) and must report early (heartbeats). Standing rule (2026-09-22, after a machine freeze): **never run more than one `smoke.sh`/`make test-san`/
real-GPU game process at a time**, across any worktree - see docs/WORKFLOW.md "One build/smoke run at a time".

## The realistic-scale rescale (user request, 2026-09-22, in progress)
User wants the star system rescaled to real astronomical distances (a red giant has millions of km between its planets; today's sun is ~1-2 km, orbits are ~30-80 km). Chosen approach: **fully realistic
scale** (not a modest bump), with **warp speed retuned much faster** (not a new time-acceleration mechanic) to keep travel times playable. Sequencing, in order:
1. ~~Double-precision ship/physics positions~~ - DONE, Leap 58.
2. Skybox-to-star-colour matching - DONE, Leap 55 (already landed before the rescale numbers themselves).
3. **Not yet started:** the actual rescale of `world/star_system`'s generation numbers (sun/planet radii, orbit distances - currently `star_system_rules.h`'s `generateSystem`), warp-speed retuning to match,
   and asteroid-belt draw-distance scaling (currently `asteroids.draw_distance` 6000 units, would be invisible at realistic scale). The render/camera-chain float-precision follow-up (see above) should
   probably land before or alongside this, since the rescale will make that jitter very visible.

## How work gets done (`docs/WORKFLOW.md`)
- Agents build in parallel worktrees and **never commit**; the coordinator reviews the diff, runs `package/tools/smoke.sh` + `make test-san` in the worktree, commits with `SG_COMMIT_APPROVED=1`, merges into `main`
  (`git merge --no-ff` is fine - `main` moves from coordinator doc commits between dispatches, a plain `--ff-only` will often fail and that's expected), rebuilds, reruns smoke + `make test-san` + a headless run on
  `main` itself, tags `good-YYYYMMDD-NN`, logs a DEVLOG entry.
- Dispatch: `orca-ide orchestration worker-start --run run_ac08f93c352e --spec "$(cat docs/agent_specs/.../wNN_*.txt)" --task-title "..." --worktree name:<worktree> --agent claude --model claude-opus-5 --effort low --display-name "..."` (use `--effort high` for foundational/risky architecture work).
  Worktrees (all merged, idle as of Leap 58): `~/orca/workspaces/space-game-v2/{p2w1-ship-core,p2w1-cockpit,p2w1-ship-hud}` - fast-forward each with `git -C <wt> fetch <repo> main && git -C <wt> merge --ff-only FETCH_HEAD` (or a plain `merge FETCH_HEAD` if the worktree has diverged commits) before a new dispatch. Specs live in `docs/agent_specs/phase6/` and `docs/agent_specs/phase7_scale/`.
- Run `run_ac08f93c352e` (bound to the coordinator terminal). Check messages with `orca-ide orchestration check --run run_ac08f93c352e --json`; a heartbeat needs no action, a `worker_done` needs the full review/merge cycle above, a `question` is answered with `orca-ide orchestration reply --run <run> --id <msg_id> --body "..."` (NOT `send --type question`).
- **If the machine crashes/freezes mid-task:** the worker's terminal dies ("failed"/"terminal_missing" in `worker-show`), but its uncommitted file changes survive on disk intact - verify with a build/test before assuming anything is lost, then dispatch a "resume" spec that tells the new worker exactly what's already done (see docs/agent_specs/phase7_scale/w56b_double_precision_resume.txt for the pattern) rather than starting over.
- Worker scratch rule: scratch files only under `logs/` with literal names; never `rm` with a variable path (Claude Code blocks it and the worker stalls).
- Never inject keys/mouse into the user's desktop (no xdotool); use `--ui-click`, saved games, dev flags. Never dispatch into a worktree with an unprocessed report or uncommitted changes sitting in it (past collision, Leap 38). Do not edit the user's assets. Ask before anything hard to reverse.

## The test laptop ("crap-top") and the retro rig
`<user>@<laptop-ip>`, key `~/.ssh/id_ed25519_spacegame_laptop`, game in the same relative path as the main rig (mirrors it; never copy game.json/settings.json). Intel HD (ILK), OpenGL 2.1, two displays (panel 1366x768 + a 1280x1024 CRT on VGA), PXN V10 wheel, SideWinder joystick, shifter + pedals later. Missing `libSDL2_ttf-2.0` was reported on this machine (open item - not yet fixed/verified, see docs/QUESTIONS.md if tracked there).
`package/tools/laptop_bench.sh [flags]` syncs the build and runs the benchmark there. Numbers before this session's Ultra/atmosphere changes: 70-75 fps on the panel (~59 on the CRT), 1% low ~23-29 - Low/Medium/High presets were explicitly kept byte-for-byte unchanged (only Ultra grew), and atmospheres are off at Low, so the laptop's numbers should be unaffected; not yet re-benchmarked to confirm.
Devices and profiles: `docs/DEVICES.md`, `docs/CONTROLLERS.md`. Leave nothing running there.

## Open items (see the newest DEVLOG entries)
**Paused:** the user wants a manual review pass (game state + bugs) before any more agent work.
Deprioritized by the user (2026-09-21 night): stick shifter, wheel pause/back button, other accessory polish.
Withheld by the user, do not design: station economy (trading/prices/contracts).
Smaller/no-decision-needed follow-ups, not yet done: laptop's missing `libSDL2_ttf-2.0`; a free-fly debug camera; unifying `ship_hud`'s ad-hoc banners onto `ui/toast`; re-benchmarking the laptop; the render/camera-chain double-precision follow-up (docs/PRECISION.md); the actual realistic-scale rescale numbers (see above).
Phase 6 loop items 1-3 (anomalies, blueprints, ore tiers) are done; see `docs/GAME_LOOPS.md` for the remaining ordered list (item 4+: solar flares, etc.) or the still-open combat depth/NPC design fork (no NPCs exist yet, weapons have nothing to fight - separate, larger decision, not blocking).

## Useful commands
`make -j8 && ./space_game_v2` · `make test-san` · `package/tools/smoke.sh` · `make joytest` (visual controller tester) · `./space_game_v2 --profile-overlay` (F3 in game) · `--list-displays` · `--list-joysticks` · `--benchmark=20 --no-vsync`.
