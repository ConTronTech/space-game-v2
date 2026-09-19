# Workflow: how phases get built

Each roadmap phase is built by **parallel agents**, then reviewed, then played:

1. **Coordinator** (Claude Code session) writes any shared contract first (e.g. `ship_api.h`), commits it to `main`,
   and gives each agent a self-contained task spec and its own git worktree + branch.
2. **Agents** build their module(s) in parallel, each in its own worktree. They report with `worker_done`.
3. **Coordinator** merges the branches into `main`, runs the smoke test, does a bug review (sanitizers, tests, code reading),
   and writes down anything wrong as a numbered **FIX LIST**.
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
- **Definition of done:** `package/tools/smoke.sh` passes on a clean tree; docs updated (`docs/<area>.md`, table row in `docs/MODULES.md`);
  small logical commits on your branch (message ends with the `Co-Authored-By` trailer used in the log); nothing pushed (there is no remote).
- **Report (`worker_done`):** 3 sentences: what you built, what you verified and how, and what you could NOT verify.
  Then list files added/changed, any contract or shared-file edits, and "manual test checklist" lines for the human player.

## FIX LIST protocol
When the coordinator sends a numbered FIX LIST: address **each item**, one commit per item (`fix(N): ...`), add or extend a test when the
bug is testable, run `package/tools/smoke.sh`, and reply per item as `N: fixed | cannot reproduce (why) | needs decision (what)`.
Do not "improve" unrelated things while fixing.
