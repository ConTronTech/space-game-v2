# Workflow: how phases get built

Each roadmap phase is built by **parallel agents**, then reviewed, then played:

1. **Coordinator** (Claude Code session) writes any shared contract first (e.g. `ship_api.h`), commits it to `main`,
   and gives each agent a self-contained task spec and its own git worktree + branch.
2. **Agents** build their module(s) in parallel, each in its own worktree. They leave their work **uncommitted** and report with `worker_done`.
3. **Coordinator** reviews each agent's uncommitted diff (sanitizers, tests, code reading), and writes down anything wrong as a numbered
   **FIX LIST**. Only after approval does the coordinator commit it and merge it into `main`, then runs the smoke test.
4. **The user** plays the game and adds to the FIX LIST.
5. The FIX LIST goes back to the agents (one list, numbered), they fix, and steps 3-5 repeat until clean. Then the roadmap boxes are ticked.

## Rules every agent follows
- **Read first:** `docs/MODULES.md`, your roadmap step in `docs/ROADMAP.md`, and the docs for services you use
  (`docs/INPUT.md`, `UI.md`, `CONFIG.md`, `SAVES.md`, `AUDIO.md`, `DATA.md`, `PHYSICS.md`).
- **Setup in a fresh worktree:** run `package/tools/link_assets.sh` (links the git-ignored `assets/` folder).
- **The old game is a read-only reference** at `/home/contolis/Documents/Space-Game-V2/space-game/space-game`. Read it for behavior and
  numbers; never edit it and never copy its globals/header-logic structure. Port the *behavior* as modules.
- **Ownership:** edit only the files your task names. A shared file (`config/input/default.json`, `data/*.json`, `docs/*.md`) may get a
  small **additive** edit for your feature; say so in your report. Do **not** tick roadmap boxes (the coordinator does).
  `package/modules/ship/ship_core/ship_api.h` is a contract: extend it only additively and only if you must; say so.
- **Architecture:** modules talk through interfaces (`core::IInput`, `IAudio`, `ISettings`, `ISaveSystem`, `IData`, `IPhysics`,
  `ICamera`, `ship::IShip`) and events; no globals, no `printf` (use `LOG_I/W/E/D`); input through *actions* in the input profile,
  not raw keys; numbers a player might tweak are `eng.config.get("module.key", default, "doc")`; content goes in `data/` JSON;
  state worth keeping implements `core::ISaveable`; register everything in `init`, undo it in `shutdown`.
- **Tests:** logic that can be tested without a window gets unit tests in `package/tests/test_<thing>.cpp` (helper types inside
  `namespace { }`). Run `make test-san`. Verify what you can in the running game with screenshots (`--screenshot=`, `--paused`).
- **NEVER COMMIT.** Agents do not commit, amend, merge, rebase, stash away work, or push. Leave every change uncommitted in your worktree
  so the coordinator can review the exact diff. A git hook refuses commits in agent worktrees; do not bypass it (`--no-verify` is forbidden).
  Commits happen only after the coordinator (or the user, for anything the user wants to see first) has approved the diff.
- **Definition of done:** `package/tools/smoke.sh` passes on the uncommitted tree; docs updated (`docs/<area>.md`; the coordinator adds the
  table row in `docs/MODULES.md`); new files are present in the working tree (no need to `git add`); nothing pushed (there is no remote).
- **Report (`worker_done`):** 3 sentences: what you built, what you verified and how, and what you could NOT verify.
  Then list files added/changed, any contract or shared-file edits, and "manual test checklist" lines for the human player.

## FIX LIST protocol
When the coordinator sends a numbered FIX LIST: address **each item**, add or extend a test when the bug is testable, run
`package/tools/smoke.sh`, leave everything uncommitted, and reply per item as `N: fixed | cannot reproduce (why) | needs decision (what)`.
Do not "improve" unrelated things while fixing.

## Agent tiers (who does what)
Not every task needs the strongest model. Orchestrated work is split by weight:

| Tier | Agent | Use for |
|---|---|---|
| Coordinator | Claude Code (this session) | contracts, merging, bug review, FIX LISTs, anything cross-module |
| Heavy workers | Claude / Codex (`worker-start --agent claude\|codex`) | whole modules (ship core, world, weapons), refactors touching several files |
| **Light workers** | **OMP + Qwen3.8 27B (Spark gateway)** | docs, single-file fixes, adding unit tests for existing code, data/JSON content (items, recipes), renames, small well-specified changes |

Check quota before launching heavy workers: `orca-ide account list --json` shows Claude/Codex usage (`rateLimits`).

### Running a Light worker (OMP + Spark)
The model is configured in `~/.omp/agent/models.yaml` as provider `spark-gateway`, model `qwen3.8-27b` (262,144-token context, reasoning on).
`omp --model spark-gateway/qwen3.8-27b` uses it. Facts checked on this machine:
- Non-interactive: `omp -p "<task>" --no-session --model spark-gateway/qwen3.8-27b < /dev/null` (**always redirect stdin**: without a
  terminal, omp waits forever at `readPipedInput`).
- It can edit files and run commands (fixed a small failing C++ test unaided in ~30 s).
- Supervised under Orca (a built-in `--agent` id does not exist for it, so use the low-level route, which still gives full supervision):
```text
orca-ide worktree create --name <name> --repo id:<repo-id> --base-branch main --no-parent --setup skip --json
orca-ide terminal create --worktree id:<worktree-id> --title spark-qwen --command "omp --model spark-gateway/qwen3.8-27b" --json
orca-ide terminal wait --terminal <handle> --for tui-idle --timeout-ms 90000 --json     # must report satisfied: true
orca-ide orchestration worker-start --run <run> --spec "<task>" --terminal <handle> --worktree id:<worktree-id> --json
```
- Give Light workers small, self-contained specs with exact file names and an acceptance check. Review their output like any other.

## Commit policy (who may commit what)
- **Agents: never.** Enforced by `package/tools/hooks/pre-commit` (install with `package/tools/install_hooks.sh`, shared by every worktree):
  in `~/orca/workspaces/*` a commit is refused unless `SG_COMMIT_APPROVED=1` is set, which only the coordinator does after reviewing the diff.
- **Coordinator:** commits only reviewed work, and only after approval. Work the user wants to see first waits for the user.
- **Main checkout:** unrestricted for the user and the coordinator.
- The hook stops accidents, not determined bypasses: `git commit --no-verify` skips it. Agent briefs forbid that, and the coordinator checks
  `git log` of each worktree when it settles.
