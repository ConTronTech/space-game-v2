# Benchmark and hardware log

The game has to run smoothly on the target laptop (see "Target hardware" in docs/VISION.md), and the fast dev machine cannot tell us whether it does. So the game
measures itself and prints a result that can be pasted into a message.

## Run it on the laptop
```
cd <game folder>
./space_game_v2 --benchmark=20 --no-vsync
```
- Runs the current scene for 20 s (after a 2 s warm-up), then prints the result block and quits. It is also saved to `logs/benchmark.txt`.
- `--no-vsync` matters: with vsync on, frames are capped at the screen's refresh rate and any headroom is hidden. Run it once with and once without if curious.
- Please also send the first lines of `logs/game.log`: they contain the real OpenGL version, the GPU name and the largest texture size (`[window] OpenGL ...`),
  which confirms the "OpenGL 2.1 is the ceiling" assumption for the laptop's Intel graphics.

## Reading the result
| Line | Meaning |
|---|---|
| average | frames per second over the run |
| 1% low | the frame rate of the slowest 1% of frames: what stutter feels like. This is the number that matters most |
| worst frame | the single slowest frame |
| target | 60 fps average, 30 fps floor (`docs/VISION.md`) |

For reference, the dev machine (RTX 2060 SUPER) does about 2,200 fps on today's small scene, so absolute numbers only mean something on the laptop.
Every time the world gets heavier (planets, asteroids, more triangles) run this again on the laptop and compare.

## Implementation
`package/modules/core/benchmark/` (`bench_stats.h` is pure and unit-tested). It measures real wall-clock frame times (the engine's own `dt` is clamped to 0.1 s and would hide hitches),
and is inert unless `--benchmark` is given. `--no-vsync` is handled by `core/window`. The startup hardware log is in `core/window`.

## Finding what is slow: `--profile`
`--benchmark` gives the totals; `--profile` (add `=gpu` to charge GPU work to the pass that caused it) prints and writes `logs/profile.txt`: average / worst ms per module hook and per render pass, and every slow frame with its three slowest parts.
See docs/PERFORMANCE.md for how to read it, how to run it on the laptop and what the performance pass changed. The benchmark itself is unchanged.

## A/B runs on the laptop (round 2)
Every performance fix has its own tunable (table in docs/PERFORMANCE.md, "Round 2"): put the keys you want to force into the laptop's `config/game.json` (`render.scale`, `render.clear_color`, `starfield.points`, `cockpit.glass_tint`), alternate the old and new settings run by run
(the laptop throttles: only alternating runs compare). Extra flags: `--fullscreen` (borderless desktop fullscreen) and `--no-vsync`; the startup log line `[window] swap interval: ...` says whether vsync is really off.
The `--profile` table header shows the render scale in use; the benchmark result block does not (the startup log line `[render] render.scale ...` does).
