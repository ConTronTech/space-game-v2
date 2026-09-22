# Performance (`--profile`, laptop pass 3.8)

Target (docs/VISION.md): 60 fps average and a 30 fps floor on the i5 M 560 / Intel Ironlake laptop at 1280x720, OpenGL 2.1 fixed function.
The dev PC cannot show a problem there, so the game measures itself: `--benchmark` (docs/BENCHMARK.md) gives the totals, `--profile` says **where the time goes**.

## Live profiler (F3 / F4 / F5): find a stutter without restarting
**F3-F5 = performance, F6-F8 = gameplay / physics state** (the in-game debugger, docs/DEBUGGER.md: same pattern, a watch panel, debug draws and `logs/debug_state.txt`).
The same `engine::Profiler` now also runs **live**, always on (`profiler.lite`, default true): a ring of the last 600 frames (about 10 s at 60 fps) with the frame time and the CPU time of every module hook and render pass, recorded without `glFinish` and without allocating.
Code: `package/engine/profiler_live.{h,cpp}` (pure ring, rolling windows, 1% low, top consumers, hitch rules, graph bars: `tests/test_profiler_live.cpp`), the recorder in `profiler.cpp`, the panel in `package/modules/core/profiler_overlay/`.

| Key (input action) | What it does |
|---|---|
| **F3** (`toggle_profiler`) | open / close the overlay (top-right corner). `--profile-overlay` starts with it open |
| **F4** (`toggle_profiler_gpu`) | while the overlay is open: GPU attribution on/off (`glFinish` around every render pass, like `--profile=gpu`). The panel says "GPU MODE (slower, ranks passes)" in amber |
| **F5** (`dump_profile`) | writes `logs/profile_live.txt` (works with the overlay closed) and shows "profile written to logs/profile_live.txt" for 4 s |

**Reading the overlay:** `FPS` = average over the last second; `frame` = the last frame; `avg / worst` over the last 5 s; `1% low` = the frame rate of the slowest 1% of frames in the last 10 s (the number that says "stutter");
the graph shows the last 120 frames, oldest on the left (blue normal, **amber above 18 ms**, **red above 33 ms**, thin lines at 16.7 ms = 60 fps and 33 ms = 30 fps);
the line under it counts **hitches** in the last 30 s (amber when there were any); `TOP CONSUMERS` are the six module hooks / render passes with the highest average CPU time per frame over the last second, with a bar (share of the frame);
the bottom lines say the quality preset, window size / fullscreen, the render scale in use and the GL renderer.
Notes: with vsync ON the frame wait is inside `core/window:present`, so it tops the list at ~16 ms even when everything is fine: look at the other rows. When the GPU is the bottleneck, its time also shows up in `present` (or the next frame's first pass), not in the pass that is heavy:
press F4 to charge GPU time to the pass that caused it (fps drops while it is on: it ranks passes, it does not measure fps). `cockpit:solid/screens/glass` are parts of `pass:ship/cockpit`; passes run inside `core/render_engine:render`, which is left out of the list.

**Hitches:** a frame slower than `profiler.hitch_ms` (default 40) is remembered (the newest 50) with its frame number, engine time, milliseconds and its three slowest parts. Not counted: the first 60 frames (loading), any frame while the game is paused, the frame after unpausing, and the frame(s) of a window resize / fullscreen switch.

**Sending a capture after a stutter:** press **F5** right after it (the ring holds the last ~10 s, so do it within a few seconds), then send `logs/profile_live.txt`. It is self-explanatory: a header (build time, uptime, quality preset and why, GL renderer and version, window size and fullscreen, vsync swap interval, render scale, profiler mode, pause state),
a "how to read this" paragraph, the aggregate table over the ring (avg / worst / % per part), the hitch list (frame, time, ms, three slowest parts) and the last 120 frame times.

**Cost:** closed, the lite recorder is about 220 clock reads and a 1 KB copy per frame (a few microseconds): no measurable change on the dev PC (0.39 ms/frame with `profiler.lite` on and off, 4 runs each, NVIDIA, vsync off), none beyond noise on the software renderer (23.2-24.2 ms vs 23.0-23.4 ms, 2 pinned cores, `--quality=low`).
Open, the panel adds one blended glass panel, ~30 text quads (the text is rebuilt 4 times a second so the UI text cache keeps its textures) and ONE GL batch for the graph: +0.05 ms on the dev PC (0.39 -> 0.44 ms/frame), about +3.5-4 ms on the software renderer (fill bound). It is a diagnosis tool: close it (F3) when not looking.
Switch the recorder off with `profiler.lite: false` in `config/game.json` (the overlay then does nothing). Dev flags for tests: `--profile-overlay`, `--profile-dump=FRAME` (as if F5 were pressed on that frame), `--profile-gpu-toggle=FRAME` (as if F4).
`--profile` / `--profile=gpu` / `--profile-slow` and `--benchmark` are unchanged: `--profile` is still the DETAILED mode (cumulative table, every slow frame, `logs/profile.txt` at exit); live and detailed share the same `Profiler` and timers.

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
`ssh <laptop> "cd ~/Documents/Space-Game-V2/space-game-v2 && export DISPLAY=:0 LD_LIBRARY_PATH=libs SDL_AUDIODRIVER=dummy && ./space_game_v2 --profile --profile-slow=20 --benchmark=20 --no-vsync > /dev/null 2>&1; cat logs/profile.txt"`
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

# Round 2: GPU / fill-rate work

Round 1 did nothing on the laptop (A/B: 33.8/35.6/35.6 fps before, 36.0/35.7/35.5 after), so the laptop is GPU / fill-rate bound. Laptop `--profile=gpu` (Low, 1280x706): skybox 11.7 ms, cockpit 10.2 (solid 3.1 + glass 2.9 + screens 2.9),
starfield 6.2, star_system 1.7, everything else < 0.3; the same ~55 ms stall appears inside whichever pass blocks.

## What each fix is, and how to switch it off (for A/B runs)
All are tunables in `config/game.json` (the laptop's `~/Documents/Space-Game-V2/space-game-v2/config/game.json`, which `laptop_bench.sh` never overwrites) and **Low-preset defaults**; High/Ultra are unchanged.
Precedence: game.json > preset > code default, so a value in game.json switches one fix on or off on its own, whatever `--quality` says.

| Fix | Tunable (game.json section `key`) | Low default | "off" (= round-1 behaviour) | What it does |
|---|---|---|---|---|
| Render scale | `render` `scale` | 0.7 (medium 0.85) | `1.0` | the 3D world is drawn into an offscreen buffer (EXT_framebuffer_object, RGBA8 + 24-bit depth) at `scale` x the window size and stretched over the window with one `GL_LINEAR` quad; UI/HUD/text stay native. No FBO extension, or scale >= 1: not created at all |
| Colour clear | `render` `clear_color` | false | `true` | skips the per-frame colour clear when the skybox (draws every pixel) or the stretched world buffer covers the whole window; depth is still cleared |
| Starfield style | `starfield` `points` | false | `true` | false = stars as tiny camera-facing quads (triangles, one draw, no point-size state) instead of `GL_POINTS` size 1.5. Also `starfield` `point_size` (1.5) and `count` |
| Canopy tint | `cockpit` `glass_tint` | false | `true` | false skips the see-through canopy draw. The canopy dome covers most of the upper screen, so it is a large blended full-screen-ish layer |
| Skybox size | `skybox` `max_size` | 512 | `1024` | (unchanged; 256 tested, see below) |
| Windowed vs fullscreen | flag `--fullscreen` | | (no flag) | borderless desktop fullscreen (`SDL_WINDOW_FULLSCREEN_DESKTOP`) at startup; the pause-menu / `video.fullscreen` setting uses the same mode |
| Vsync | flag `--no-vsync`; env `vblank_mode=0` | | | the log now says what the driver did (see below) |

Example laptop `config/game.json` for one A/B leg (only the keys you list change; delete a line to go back to the preset):
```json
{ "render": { "scale": 1.0, "clear_color": true }, "starfield": { "points": true }, "cockpit": { "glass_tint": true } }
```
Whole-preset comparison: `--quality=low` / `--quality=medium` / `--quality=high` (each sets all keys above at once, unless game.json overrides them).
Round 1 equivalent: `render.scale 1.0, render.clear_color true, starfield.points true, cockpit.glass_tint true`.
`--profile` / `--profile=gpu` print a `settings:` line in the table header with the render scale and clear policy in use, and a `pass:render.scale composite` row when scaling is on.

## Dev-PC numbers (llvmpipe, 2 pinned cores, `--quality=low`, 3 runs of 6 s, ms per frame; noise about +-0.7)
| Config | runs | ms |
|---|---|---|
| A: round-1 behaviour (scale 1.0, clear on, points, glass on) | 13.9 13.7 13.3 (again 14.2 15.2 13.8) | ~14.0 |
| B: + `clear_color` false | 14.9 13.5 15.0 | ~14.5 (no gain on this renderer) |
| C: + starfield quads | 15.1 13.6 13.2 | ~14.0 (no change; the stars cost 0.15 ms here) |
| D: + `glass_tint` false | 11.6 11.0 11.8 | **~11.5 (-2.5 ms)** |
| E: + `render.scale` 0.7 | 13.1 13.6 13.6 | ~13.4 (-0.6; a separate run pair gave 14.0 -> 12.2) |
| F: all Low defaults | 11.2 11.2 11.4 | **~11.2 (-2.8 ms)** |
| G: F with scale 0.5 | 9.9 9.7 9.9 | **~9.8 (-4.2 ms)** |
| skybox.max_size 512 / 256 / 128 | 13.8 / 14.6 / 13.8 | no effect on this renderer |

llvmpipe is not the laptop's GPU: it is raster-bound but has large fixed costs per pass (which is why scale 0.7 shows only ~-1 ms), and starfield points are cheap in it. So these numbers show the direction, the laptop A/B decides.
The big finding is the **canopy glass**: my round-1 pixel estimate said "4% of the screen" because it ignored triangles that cross the near plane; the dome actually covers most of the upper view (the blue tint in the sky), and removing it saves as much as anything.

## Findings, item by item
1. **Render scale**: `RenderEngine` owns the offscreen buffer (`render_scale.h` has the pure size maths). Recreated when the window or scale changes (resize, fullscreen toggle: tested with `--fullscreen`, buffer 1344x756 in a 1920x1080 window).
   The world passes and the cockpit run in the buffer (the cockpit's own depth clear works on it); `onRenderUI` panels draw afterwards on the window at native resolution. The screenshot flag reads the window, so screenshots show the composed frame.
   Cost when off: none. Cost when on: one full-screen textured quad (`pass:render.scale composite` in the profile), against the sky/world/cockpit shrinking to `scale^2` of their pixels.
2. **Starfield 6.2 ms**: probable cause is `glPointSize(1.5)` (a non-1 point size is a slow path on some old Intel drivers) plus a state push of point+line state; **not proven on the laptop**. Fix: quads path (`starfield.points=false`) - one `glDrawArrays(GL_QUADS)`, static vertex array rebuilt only when the viewport height or fov changes,
   size in pixels kept (`starfield.point_size`), no point/line state. The warp-streak line pass only runs while streaking (it already did). Laptop test: A/B `starfield.points`.
3. **Cockpit glass / solid**: glass is now optional (`cockpit.glass_tint`, off at Low). The hull is 166 triangles (log line `[cockpit] N triangles`), so there is nothing to LOD; its 3.1 ms on the laptop is fill (the hull covers a large part of the view, overdraw ~1.35) which render.scale reduces.
   The screens (2.9 ms there) are ~15% of the screen area; they are not blended more than once each.
4. **Skybox 11.7 ms**: 1 to 3 visible full-screen textured faces. Skipping the colour clear (item above) and render.scale are the levers; face size does not matter on llvmpipe (512/256/128 identical), so Low stays at 512. Still not tried: GL_RGB5 textures, drawing the sky last with depth test.
5. **Present / compositor**: window.cpp now logs the swap interval the driver actually has (`[window] swap interval: asked for 0, driver accepted it, now 0`; llvmpipe answers "REFUSED, now 0" for vsync). If the laptop says `(vsync is still on ...)` that is the ~60 fps cap.
   Otherwise the cap is the compositor (Cinnamon/Muffin): try `vblank_mode=0` in the environment (Mesa's own override), `--fullscreen` (borderless desktop fullscreen lets the compositor unredirect the window; the old fullscreen setting already used `SDL_WINDOW_FULLSCREEN_DESKTOP`), and windowed vs fullscreen A/B. SDL already asks the window manager to bypass the compositor for fullscreen windows by default.
6. **Profiler** prints the render scale and clear policy in a `settings:` header line.

## Not done
* A generic `--set key=value` flag would make one-off A/B legs easier than editing `config/game.json` on the laptop; it needs a small change in `package/engine` (not in this task's ownership).
* GL_RGB5 sky textures, and drawing the sky last with the depth test (only fills pixels nothing else covered): both change how the frame is built or looks; wait for the laptop numbers.
* The fixed ~55 ms stall inside whichever pass blocks is still unexplained; a driver/compositor stall is the best guess. If `render.scale` and the clear changes do not remove it, run `--profile=gpu --profile-slow=40` and `vblank_mode=0`.
