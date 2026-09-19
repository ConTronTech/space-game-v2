# Qwen3.8 27B (Spark gateway) - light-worker report

Tested 2026-09-19 through OMP (`omp --model spark-gateway/qwen3.8-27b`), 262K context. It is **free inference** and the gateway allows **at most 2
concurrent agents**; it uses no Claude quota, which is why it is the right home for light tasks.
Every result below was **verified independently by the coordinator** (re-running builds/tests, diffing files), never taken from Qwen's own report.

## Verdict
**Good at:** reviewing code against the project rules, data/JSON edits, following a precise spec, small localized fixes when pointed at the
right place, explaining a root cause correctly, and following the Orca worker protocol (heartbeat, `worker_done`, FIX LIST).
**Weak at:** speed (minutes per small task), unguided debugging in unfamiliar code, subtle test-writing (float comparisons), staying inside
"only edit these files", and **honest self-verification** (it reports "verified" when it is not).
**Use it instead of Claude for:** reviews, data, docs, small fixes, unit tests for existing code. **Not for:** whole modules, cross-module
refactors, architecture decisions, physics/math-heavy debugging without pointers.

## Scorecard
| # | Task | Result | Time | Notes |
|---|---|---|---|---|
| 1 | Trivial arithmetic prompt | correct | 13 s | gateway sanity check |
| 2 | Fix a failing C++ helper (swap lo/hi), scratch folder | **pass** | 29 s | minimal fix, did not touch the test, verified by me |
| 3 | Write `docs/QUICKSTART.md` from the repo (Orca-supervised worker) | **mostly pass** | 2 min 58 s | scope and structure right; 5 factual inaccuracies (see below) |
| 3b | Apply my 7-item FIX LIST | **pass 7/7** | 6 min 9 s | every fix correct against the source; followed the protocol |
| 4 | Add an ore + item + recipe to `data/*.json`, run tests | **pass, perfect** | 417 s* | exactly 3 files, correct values, matched column alignment, tests green |
| 5 | Add `angleBetween()` + unit tests | **partial** | timed out (420 s) x2 | implementation correct and NaN-safe; **its own tests fail** (see below) |
| 6 | Find + fix a planted bug in the physics solver | **fail, then pass** | 2 timeouts; 261 s with hints | see below |
| 7 | Review a module with 7 planted defects | **pass 7/7, 0 false positives** | 325 s | cited the project's own rules with line numbers |

\* started together with 3 other trials, so it was competing with them for the 2 gateway slots and for CPU.

## Strengths (evidence)
- **Code review is its best skill.** Found all 7 planted defects (dangling reference, uninitialised member, `pressed()` in `onFixedUpdate`,
  missing `dependencies()`, unchecked null `get<>()`, missing `shutdown()` unregister, `printf` in a module), each with the correct fix and the
  correct rule cited (`input_handler.h:7`, `docs/MODULES.md`). It even ranked them by severity and said "nothing else is a defect".
- **Precise data edits:** matched the existing style including padding and trailing-comma handling; respected "do not edit any other file".
- **Root-cause explanation (trial 6) was exactly right:** "picked the exit root of the quadratic instead of the entry root", one-character
  fix, file identical to the original afterwards, tests untouched.
- **Protocol discipline:** used the Orca heartbeat/`worker_done`, answered the FIX LIST per item, kept edits inside the named file.
- **It reads the repo docs and applies them** (module rules, input rules) without being told which rule applies.
- Tool use (edit files, build, run tests) works well through OMP.

## Weaknesses (evidence)
1. **Slow and variable.** 13-30 s for trivial work, 3-7 minutes for anything with a build. Claude finishes the same tasks in seconds.
2. **Hard limit of 2 concurrent agents** (gateway). I started four at once, so two were queued behind the others and 3 of 4 hit my 7-minute cap:
   that was the queue, not the model. Run **at most 2 at a time**; a third simply waits. (Two at once also compete for CPU during `make test`.)
3. **Overconfident self-report.** Trial 3 reported "no uncertainties, all facts verified" but had 5 inaccuracies (smoke.sh described as a
   "sanitizer build", `game.json` mislabelled, `data/` incomplete, module folders incomplete, "sounds" in assets). Always verify.
4. **Test-writing pitfalls.** Trial 5 asserted `angleBetween(parallel) == 0.0f` exactly; float `acos` near 1 gives ~3e-4, so its own two
   assertions fail. It wrote a brute-force random-search program (loop capped at 400 million iterations) to find a float case instead of simply choosing a tolerance, and never reported.
5. **Breaks "only edit these files".** Left a stray `find_cos.cpp` scratch file in the repo root.
6. **Unguided debugging stalls.** Trial 6 timed out twice with just "tests fail, find the bug". It succeeded once told where the code lives
   and to build with `make -j8 test`. Most of its time goes to slow single-threaded builds and reading.
7. **Non-interactive quirk:** `omp -p` without a terminal hangs at `readPipedInput` unless stdin is closed (`< /dev/null`).

## How to use it
**Give Qwen:** code review / rule-lint of a diff (a great cheap second opinion on Claude's work), data and JSON content, docs generated from
existing files (then a verification pass), single-file bug fixes with the failing test named and the folder pointed out, adding tests for
existing code, mechanical renames.
**Keep on Claude/Codex:** new modules, anything touching several modules, contracts/interfaces, physics/math-heavy debugging, anything where a
wrong answer is expensive to spot.

**Task brief checklist for Qwen**
- Name the exact files it may edit and say "do not create other files; delete any scratch file you make".
- Name the failing test/command, the folder to look in, and say `make -j8 test`.
- For float tests: "compare with a tolerance (e.g. 1e-4), never exact equality".
- Ask for the report in 2 sentences, then **verify yourself**: `git diff --stat`, re-run tests (`make -j8 test-san`).
- At most 2 workers at once (gateway limit), 7-minute cap, `< /dev/null` when scripting, never let it commit (the pre-commit hook enforces this in worktrees).

## Suggested split while Claude quota is low (Phase 2)
| Task | Owner | Why |
|---|---|---|
| OBJ/MTL parser + tests (`obj_parser.h/.cpp`, pure logic) | Qwen | well-specified, testable; give the float-tolerance rule |
| `hud_logic.h` (thresholds, colours) + tests | Qwen | pure logic |
| Review of every diff before I commit | Qwen | its strongest skill |
| Docs, data | Qwen | proven |
| `ship_core`, warp, respawn, HUD drawing, cockpit rendering, integration | Claude (after reset) or Codex | cross-module, needs judgement |

## Caveats on this report
Small sample (7 tasks, one run each, planted bugs). Timings depend on gateway load (a shared community gateway, 2 concurrent agents max). Trials 4-7 ran in scratch copies
of the repo, not in worktrees. Re-test after a model or gateway change.
