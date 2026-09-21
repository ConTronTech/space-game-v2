# Resume guide (updated 2026-09-20, end of Phase 2 Wave 1)

## Where the project stands
- `main` of `/home/contolis/Documents/Space-Game-V2/space-game-v2` is the real project (the old `space-game/space-game` folder is a read-only concept reference).
- Phases 0-1 are complete. **Phase 2 Wave 1 is complete and merged:** `ship/ship_core` (real ship: HP, shield, fuel storage, collision damage, death),
  `ui/ship_hud` (glass HUD; steps back in cockpit view) and `ship/cockpit` (ShipV2 interior with live `@HUD` screens). 140 unit tests, green under ASan/UBSan.
- **Default setup:** cockpit view with the ShipV2 model, the HUD on the ship's own screens (INFO / SYSTEMS / RADAR). `V` toggles chase view (full flat overlay).
- **Next: Wave 2** = `ship/warp_drive` (2.2) and `ship/respawn` (2.4), then the user's playtest FIX LIST. Then Phase 3 (world). See `docs/ROADMAP.md`.
- Known gaps: no way to gain shield/fuel yet (inventory is Phase 5); death ends on a frozen screen until 2.4; the radar shows NO CONTACTS until the world exists;
  audio verified only with SDL's dummy driver.
- Parked (later): local multiplayer, mobile app, browser build - `docs/ROADMAP.md` "Parked ideas".

## How work gets done (`docs/WORKFLOW.md`)
Agents build in parallel worktrees and **never commit**; the coordinator reviews the diff, runs smoke + sanitizers, then commits (`SG_COMMIT_APPROVED=1`) and merges;
the user plays; problems go back as one numbered FIX LIST. Task specs live in `docs/agent_specs/` (Wave 1 specs are the template for Wave 2).
Qwen (free, max 2 concurrent) is a good reviewer of SMALL pieces only: `docs/QWEN_REPORT.md`.

## Orca state (repo id `4901aa59-f2a8-4d6b-a1f1-1f91fa5d2e8a`)
- Run `run_ac08f93c352e` (bound to the coordinator terminal of the 2026-09-20 session; a new session needs a NEW run: `orca-ide orchestration run-create`).
- Three **retained** Claude workers with full context of their code, ready for FIX LISTS: `p2w1-ship-core`, `p2w1-ship-hud`, `p2w1-cockpit` (worktrees under `~/orca/workspaces/space-game-v2/`).
  Their branches are merged into `main`. To reuse one, fast-forward its worktree (`git -C <wt> merge --ff-only main`), `task-create`, then `worker-start --task <id> --terminal <handle> --worktree id:<repo-id>::<path>`.
- Old leftovers, safe to remove: worktrees `p2-ship-core` (failed first attempt) and `spark-quickstart` (Qwen trial, merged).
- Use `orca-ide` (never bare `orca`). Read the skill guide with `orca-ide skills get orchestration`.

## Useful commands
- Build/run: `make -j8 && ./space_game_v2`; tests: `make test-san`; full check: `package/tools/smoke.sh`.
- Dev flags: `--fake-ship=hp:35,shield:120,fuel:10[,dead][,hit:20]`, `--paused[=settings|load]`, `--screenshot=f.bmp --screenshot-frame=N`,
  `--ui-click=X,Y,FRAME[;...]` (injects menu clicks; Load Game is at 640,388 at 1280x720).
