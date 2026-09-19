# Resume guide (written 2026-09-19, before Claude's weekly limit reset, Mon 6:00 AM)

## Where the project stands
- `main` branch of `/home/contolis/Documents/Space-Game-V2/space-game-v2` is the real project. The old `space-game/space-game` folder is a **read-only concept reference**.
- Phases 0 and 1 are complete: 15 modules, 78 unit tests, all green under ASan/UBSan (`package/tools/smoke.sh`). See `docs/ROADMAP.md`.
- Phase 2 (ship) is **designed but not started**: contract `package/modules/ship/ship_core/ship_api.h` is committed; task specs for Wave 1 are in
  `docs/agent_specs/phase2_wave1/` (`w1_ship_core`, `w2_ship_hud`, `w3_cockpit`). Wave 2 (warp drive 2.2, respawn 2.4) comes after ship_core is merged.
- Process: `docs/WORKFLOW.md`. Agents build in parallel worktrees and **never commit**; the coordinator reviews the diff, then commits (`SG_COMMIT_APPROVED=1 git commit`).
- Qwen (Spark gateway) evaluation and how to use it: `docs/QWEN_REPORT.md`.

## Orca state left behind (Orca repo id `4901aa59-f2a8-4d6b-a1f1-1f91fa5d2e8a`, run `run_1c7f951d082c`)
- Worktree `p2-ship-core`: the **stalled first attempt** (dispatch `ctx_c208dc4c0320`, failed at agent readiness). Its Claude terminal showed the weekly-limit
  notice. Nothing was written there. Safe to remove.
- Worktree `spark-quickstart`: the Qwen trial; its work is merged into `main`. Safe to remove.
- Neither was removed because that wasn't approved.

## Agents and quota
- Claude: weekly limit was at 96% (resets Mon 6:00 AM). Check first: `orca-ide account list --json` (see `rateLimits`).
- Codex: signed in (Free plan), quota unreadable when checked. Cursor and OpenCode are installed, untested.
- Qwen3.8 27B via Spark gateway (`omp --model spark-gateway/qwen3.8-27b`): free inference, **max 2 concurrent agents**, slow, good reviewer. Read the report before delegating.

## To resume Phase 2
1. Check quota (above). Optionally remove the two leftover worktrees.
2. Start Wave 1 workers one at a time (worker-start creates the worktree and agent terminal; use the saved specs):
   ```text
   orca-ide orchestration worker-start --run run_1c7f951d082c --spec "$(cat docs/agent_specs/phase2_wave1/w1_ship_core.txt)" \
     --task-title "2.1 ship/ship_core" --worktree new-top-level --repo id:4901aa59-f2a8-4d6b-a1f1-1f91fa5d2e8a \
     --base-branch main --name p2-ship-core --agent claude --setup skip --json
   ```
   (the worktree name `p2-ship-core` exists already; remove it first or use another `--name`). If a start fails at `agent_readiness`, do NOT relaunch:
   read the terminal (`orca-ide terminal read --terminal <handle>`); a quota notice or a "trust this folder" prompt is the likely cause.
3. Wait for `worker_done` (`orca-ide orchestration check --wait ...`). Then review the **uncommitted** diff in each worktree
   (`git -C <worktree> status` / `diff`), run `package/tools/smoke.sh` there, write a numbered FIX LIST for anything wrong, and send it to the same worker.
4. When approved: commit in the worktree with `SG_COMMIT_APPROVED=1`, merge into `main`, run smoke, tick the roadmap boxes, release the worker.
5. The user plays the build; their findings join the FIX LIST. Then Wave 2.
6. Cheap in the meantime: use Qwen for review of every diff, docs, data (see the report's task-brief checklist).
