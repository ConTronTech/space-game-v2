# Performance (`--profile`, laptop pass 3.8)

Target (docs/VISION.md): 60 fps average and a 30 fps floor on the i5 M 560 / Intel Ironlake laptop at 1280x720, OpenGL 2.1 fixed function.
The dev PC cannot show a problem there, so the game measures itself: `--benchmark` (docs/BENCHMARK.md) gives the totals, `--profile` says **where the time goes**.

## The profiler (`--profile`)
Inert unless the flag is given (no cost otherwise). Code: `package/engine/profiler.{h,cpp}` (pure, unit-tested), timers in `Engine::run` and `RenderEngine::onRender`.
```
./space_game_v2 --profile --benchmark=20 --no-vsync                  # CPU wall-clock time per module phase and per render pass
./space_game_v2 --profile=gpu --benchmark=20 --no-vsync              # + glFinish around every render pass: GPU work is charged to the pass that caused it
./space_game_v2 --profile --profile-slow=20 --benchmark=60           # list frames slower than 20 ms instead of the default 33 ms
```
At exit it prints the table and writes `logs/profile.txt`:
* `module:hook` rows (`core/window:present`, `ship/cockpit:update`...) = time inside that module's hook, every frame. `engine:unaccounted` = frame time not inside any hook.
* `pass:name` rows = each RenderEngine pass (`pass:skybox`, `pass:ship/cockpit`, ...). They run **inside** `core/render_engine:render`, so do not add the two. `cockpit:solid/screens/glass` are parts of the cockpit pass.
* columns: average ms, worst ms, % of the average frame (the first 30 frames, which load assets, are left out of the averages).
* `SLOW FRAMES`: every frame slower than the threshold with its frame number and its three slowest parts. **This finds hitches.**

**Read it correctly.** The GPU works asynchronously: without `=gpu` most GPU time shows up in `core/window:present` (the buffer swap waits for the GPU) or in the next frame's first draw.
`--profile=gpu` fixes the attribution but serialises CPU and GPU, so its absolute fps is lower than a normal run: use it to rank passes, not to report fps.

## On the laptop
`package/tools/laptop_bench.sh` syncs the build and prints only the benchmark lines, so for the profile tables run it once (`SYNC=1`, any flags), then run the game there and print the file:
`ssh <laptop> "cd ~/space-game-v2-test && export DISPLAY=:0 LD_LIBRARY_PATH=libs SDL_AUDIODRIVER=dummy && ./space_game_v2 --profile --profile-slow=20 --benchmark=20 --no-vsync > /dev/null 2>&1; cat logs/profile.txt"`
and the same with `--profile=gpu`. Please send back: the table from `--profile`, the table from `--profile=gpu`, and the SLOW FRAMES list. Compare `pass:skybox`, `pass:ship/cockpit` and `core/window:present`.

## Reproducing "laptop-like" numbers on the dev PC
Mesa's software renderer is fill-rate bound like the laptop GPU, and it reproduced the laptop's split well (skybox ~4 ms, cockpit ~3-4 ms of a ~14 ms frame):
```
__GLX_VENDOR_LIBRARY_NAME=mesa LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 SDL_AUDIODRIVER=dummy taskset -c 18,19 \
  ./space_game_v2 --benchmark=10 --no-vsync --quality=low        # (auto also picks Low for llvmpipe)
```
Use a fixed pair of idle cores and 3+ runs: other programs on the machine move single runs by +-1 ms, and llvmpipe's threading makes sub-millisecond effects unreliable
(for example a pass that draws nothing but clears depth was *faster* than no pass at all).

## What was changed (pass 3.8, round 1)
| Where | Change | Why |
|---|---|---|
| `core/render_engine` | passes are `shared_ptr` entries and the per-frame working list is a reused member | it copied the whole pass vector (strings + `std::function`s) every frame: heap allocation per frame |
| `ship/cockpit` (pass) | `glPushAttrib(GL_ALL_ATTRIB_BITS)` + client attrib -> only the groups the pass changes | ALL_ATTRIB_BITS copies every state group; slow in Mesa |
| `ship/cockpit` (model) | opaque hull + light state and the glass are compiled into **display lists** (one `glCallList` each); rebuilt only if the light changes | 170+ vertices of immediate mode and ~25 state calls per frame |
| `ship/cockpit` (screens) | `ScreenCanvas` no longer calls GL: it records coloured quads into a `ScreenMesh`; each screen is redrawn at `cockpit.screen_hz` and drawn with **one** `glDrawArrays` | the three screens were ~250 `glBegin/glEnd` batches per frame; text used a fresh vector per string |
| `world/skybox` | only the faces that intersect the view frustum are drawn (`world::visibleFaces`, exact plane test, brute-force tested); `GL_TEXTURE_BIT` push removed | fewer calls/binds. Off-screen faces were clipped anyway, so this saves CPU, **not fill**: the skybox's cost on the laptop is the full-screen textured fill (see below) |
| `core/ui_handler` | text cache keyed by (size, string) with allocation-free lookups (`text_cache.h`); pixel widths cached; a full cache evicts strings unused for 60 frames instead of flushing everything | a `"<size>:<text>"` key string was built per text call; `TTF_SizeUTF8` ran for every centred string every frame; the flush regenerated every visible string in one frame (a hitch) |
| `core/quality` | preset row `cockpit.screen_hz`: low 15, medium 30, high 120, ultra 240 (= every frame) | slow machines redraw the cockpit screens less often; fast ones look exactly as before |

Dev PC, `--quality=low`, CPU time per frame (`--profile`, RTX 2060 SUPER; the GPU is not the limit here): cockpit pass 0.101 -> 0.021 ms, `ui` 0.046 -> 0.020 ms, whole frame 0.44 -> 0.28 ms (2,260 -> 3,520 fps).
Software renderer (`llvmpipe`, 2 pinned cores, `--quality=low`, mean of 4 runs): 14.1-14.6 ms before, 14.2 ms after: **no change, because that renderer is raster-bound**. So the round-1 wins are the CPU / driver-call
side (the laptop's CPU is ~5x slower than the dev PC's and Mesa's Ironlake driver adds per-call overhead); whether they show up in the laptop's 6.5 ms cockpit is exactly what the next benchmark tells us.

## Not done / findings
* **The ~65 ms hitch** (6 of 640 frames, in every variant on the laptop): NOT reproduced on the dev PC (NVIDIA: no frame over 17 ms; llvmpipe: bursts of 50-80 ms in `core/window:present` on a busy machine).
  Code review found no periodic work in any module (no timers, autosaves, logging or allocation loops in the frame path; the text-cache flush and the per-frame pass-vector copy were the only candidates and are fixed).
  The strongest candidate is the buffer swap (`core/window:present`) blocking on the X server / compositor, which no module can fix. `--profile --profile-slow=20` on the laptop names the culprit for each slow frame.
* **Base cost (12.8 ms with the world off)**: the profiler shows `core/window:present` at 60% of the dev PC's frame and nearly all of llvmpipe's; the frame's `glClear(COLOR|DEPTH)` and the swap live in `core/window` (not part of this task).
  Ideas for the owner of `core/window`: skip the colour clear (the skybox overwrites every pixel), request no stencil/alpha, try `SDL_GL_SwapWindow` with vsync on to see whether the compositor paces it.
* **Skybox fill**: a full-screen textured quad per frame at 1366x768. Options if the next benchmark still shows ~4 ms: draw the sky last with depth test (only where nothing else was drawn; needs the star/blend passes reordered),
  or a 256 px `skybox.max_size` on Low. Not done because either changes how the frame looks.
