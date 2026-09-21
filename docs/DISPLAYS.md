# Displays, video modes and aspect ratios (`core/window`, phase 1.8)

The game knows WHICH screen it runs on, how that screen is shaped, and adapts. Motivation: the retro rig (docs/DEVICES.md): a laptop with a 1366x768 panel (LVDS-1, 16:9) and a 1280x1024 CRT (VGA-1, 5:4) as one 2646x1024 desktop, later 4:3 CRT modes (640x480 .. 1024x768), and normal PCs with one or more monitors.

## Model (`core/window/display_rules.h`, pure, unit-tested in `tests/test_display.cpp`)
`DisplayInfo { index, name, x, y, w, h, refreshHz, dpiX/dpiY, modes[], fake }` per display, built by `core/window` from SDL (`SDL_GetNumVideoDisplays`, `GetDisplayName`, `GetDisplayBounds`, `GetDesktopDisplayMode`, `GetDisplayMode`, `GetDisplayDPI`) or from `--fake-displays`. Pure functions:
| Function | What |
|---|---|
| `classifyAspect(w,h)` | 4:3, 5:4, 16:10, 16:9 (1366x768 counts), ultrawide (>= 2.1), other |
| `isRetro(display)` | 4:3 or 5:4, refresh >= 60 Hz, <= 1280 wide. **Only a hint** for logs: nothing else is guessed |
| `chooseDisplay(...)` | flag `--display=N` > setting `video.display` > auto; auto = the display the mouse is on, else display 0; a display that does not exist falls back to auto and the log says so |
| `parseVideoMode`, `parseResolution` | `borderless / exclusive / windowed`; `1024x768` or `1024x768@85` |
| `closestMode` | the display mode nearest the wanted size (squared distance), then the nearest refresh (no refresh asked = the highest) |
| `selectableResolutions` | one entry per size with its best refresh, nothing under 640x480, biggest first (the settings page list) |
| `centerOn`, `fitWindow` | the window's top-left centred ON a display (non-zero and negative origins work), and a size that never exceeds the display |
| `parseFakeDisplays` | `1366x768;1280x1024`: fake displays laid out side by side |

## Settings and flags
Display numbers in keys and flags are **0-based SDL indices**; the settings page shows them 1-based ("Display 2").
| Setting (config/settings.json) | Flag | Meaning |
|---|---|---|
| `video.display` = `auto` / `0` / `1`... | `--display=N` | which display the window opens on (and where "move to display" goes) |
| `video.mode` = `auto` / `borderless` / `exclusive` / `windowed` | `--video-mode=` | `borderless` = `SDL_WINDOW_FULLSCREEN_DESKTOP` (no mode change, skips the compositor: on the laptop windowed -> borderless was 56 -> 80 fps, 1% low 16 -> 29); `exclusive` = a real display mode switch to the mode closest to `video.resolution`; `windowed`. `auto` (default) = windowed unless `video.fullscreen` / `--fullscreen` |
| `video.resolution` = `1024x768@85` | `--resolution=WxH[@Hz]` | the exclusive mode wanted; with `windowed` the flag sets the window size |
| `video.window_size` = `1280x720` | (`--resolution`) | windowed size, default 1280x720, never larger than the display |
| `video.fullscreen` (true/false) | `--fullscreen` | exactly as before: Borderless on / off. The Settings page keeps `video.mode` in step |
| `video.fov` | | vertical field of view at 16:9 (below) |
| tunable `camera.fov_mode` = `horplus` (default) / `vertical` | | field of view rule (below) |
Also `--no-vsync` (unchanged; the log line `[window] swap interval: ...` says what the driver did).

**Dev flags:** `--list-displays` prints the display block and **exits before any GL window exists** (SDL video only: works over SSH with `DISPLAY=:0`); `--list-displays=json` prints machine-readable JSON to stdout (`displays[]` with bounds, refresh, dpi, mm, aspect, retro, chosen, modes[]);
`--fake-displays=1366x768;1280x1024` feeds the pure logic a fake list (combine with `--display=1`, `--list-displays`): the window still opens on a real display, but the fake display's size is the default window size and the quality / log lines see the fake display, so 5:4 and 4:3 paths can be tested on a single-monitor machine.

## What the window does (`window.cpp`)
* Startup: reads the displays, chooses one, applies mode / size **before the GL context** (window created with `SDL_WINDOWPOS_CENTERED_DISPLAY(n)`; borderless via the creation flag; exclusive via `SDL_SetWindowDisplayMode` + `SDL_WINDOW_FULLSCREEN` while hidden), then logs **one block** (`[display]`): every display (name, bounds, refresh, DPI/size, aspect, mode count, `[retro/CRT-like]`), which was chosen and why, the mode, window / drawable size, aspect, swap interval, refresh.
* Live: settings changes (`video.mode`, `video.resolution`, `video.window_size`, `video.display`, `video.fullscreen`) apply immediately (no restart); `moveToDisplay` leaves fullscreen, centres on the target display and re-enters it. `SDL_WINDOWEVENT_DISPLAY_CHANGED` (dragged to another display, plug / unplug) re-reads the list, logs the block and emits `core::DisplayChanged`. Viewport, the render.scale buffer and the UI follow the window size every frame.
* Service `core::IDisplays` (`window_api.h`): `list()`, `chosen()`, `current()`, `drawableSize()`, `refreshHz()`, `aspect()`. `core/quality` reads the CHOSEN display's size / refresh from it and logs display name, refresh, aspect, retro and the display count. Quality presets are unchanged: Ironlake is still Low (no rule was added: nothing to justify a "low pixel count prefers cheap presets" rule).

## Aspect handling
* **Field of view** (`render_engine/fov_rules.h`): `video.fov` (default 90) is a VERTICAL fov at 16:9 (horizontal 121.9 degrees). `horplus` (default): on screens narrower than 16:9 the HORIZONTAL fov of that reference is held, so the vertical fov grows (16:10 96.4, 4:3 106.3, 5:4 109.8 degrees at 90): nothing is cropped or zoomed in; on wider screens the vertical fov is held (classic Hor+, 21:9 = 133 degrees wide) and the horizontal fov is capped at 140 degrees (32:9 gets a 75.9 degree vertical fov). 16:9 is unchanged. `vertical` = the old behaviour everywhere.
  `camera.fovDeg` is now the EFFECTIVE vertical fov of the frame (what gluPerspective and every pass read); `camera.baseFovDeg` is the setting (the Settings slider edits it).
* **UI:** `ui_handler` has `uiLayoutScale(w,h)` = min(w/1280, h/720) clamped 0.6..3 (the HUD uses the same formula); `UIHandler::scale` is set every frame and the button / toggle / slider widgets size themselves with it. The pause menu scales everything and puts the settings page in **two columns** when it does not fit (up to 12 rows).
  Audited at 1280x1024, 800x600, 640x480, 1366x768, 1920x1080: HUD, hint bar, weapon panel, cockpit screens (they are 3D geometry: independent of the aspect) and the pause menu fit without clipping or overlap. Nothing in `ui/ship_hud` needed a change.
* **Settings page** (Esc > Settings): Display (only with 2+ displays; cycles Auto, 1, 2 ...), Mode (Windowed / Borderless / Exclusive), Resolution (cycles the chosen display's list in Exclusive or Windowed; "desktop" in Borderless). All apply live, none needs a restart.

## Testing on the CRT + laptop rig
1. `./space_game_v2 --list-displays` (or `=json`) over SSH: expect VGA-1 1280x1024 `5:4 [retro]` and LVDS-1 1366x768 `16:9`.
2. `--display=0` (CRT) and `--display=1` (panel): the window opens centred on that display. `--video-mode=borderless --display=0` fills the CRT without a mode change.
3. `--video-mode=exclusive --resolution=1024x768@85 --display=0`: the CRT switches mode; the log block prints the mode actually set (and warns if it had to pick the closest). Try 800x600 and 640x480.
4. Check 5:4 / 4:3 look: the whole cockpit visible, HUD and menus inside the screen, Settings page in two columns at 640x480.
5. Drag the window between the displays: the log prints "the window is now on display N" and the block again.
6. `logs/game.log` first lines: the `[display]` block and `[quality] hardware: ... display ...`.
Not verified on this machine (it has three ordinary monitors, no CRT): exclusive mode switching, the two-display placement (covered by unit tests with a display at a non-zero origin), plug / unplug events.
