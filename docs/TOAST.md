# Toasts (`ui/toast`, roadmap 5.2)

A generic notification popup service. Any module can opt in through `core::IToast` (`package/modules/ui/toast/toast_api.h`);
nothing is wired to it yet. It is optional, the same pattern as `core::IDebug`: look it up, do nothing if it is absent.

```cpp
#include "ui/toast/toast_api.h"
// e.g. in a crafting module, when a recipe unlocks:
if (auto* t = eng.services.get<core::IToast>())
    t->show("Recipe learned: FUEL CELL");                                // Info, default lifetime
// t->show("Cargo nearly full", core::IToast::Level::Warning);
// t->show("HULL BREACH", core::IToast::Level::Urgent, 6.0f);            // 6 s instead of toast.seconds
```
Add `"ui/toast"` to your module's `optionalDependencies()` if you show a toast from `init`. Each `show` call is a new toast: call it on an
event, not every frame.

## API
| call | does |
|---|---|
| `show(text, level = Info, seconds = 0)` | queue a toast; `seconds <= 0` uses `toast.seconds` |
| `clear()` | remove every visible and waiting toast |

## Levels
| Level | look |
|---|---|
| `Info` | calm cyan text + stripe (the HUD's `kCalm` 0.45, 0.85, 1.0) |
| `Warning` | amber (1.0, 0.75, 0.2), steady |
| `Urgent` | red (1.0, 0.25, 0.22), flashing 0.55 .. 1 like the HUD's warning banners |

Colours are copied from the conventions in `ui/ship_hud/hud_logic.h`, not included: the module stays decoupled.

## Behaviour
- **Position: bottom-right corner, newest at the bottom**, older toasts pushed up, right-aligned, lifted above the bottom-centre controls hint
  row. Why there: top-left = speed/bars (and the F6 debugger), top-right = orbit guide (and the F3 profiler), top-centre = the HUD's banner
  stack, bottom-left = cargo/weapon block, bottom-centre = controls hint. Bottom-right was the one free area.
- At most `toast.max_visible` (4) on screen; more **wait in a queue** (they do not age while waiting) and appear as older ones expire.
- The queue holds 16 in total; when full, the **oldest waiting** toast is dropped (never one being read), a warning is logged.
- Each toast fades linearly over the last 0.5 s of its life (a shorter life fades over all of it).
- Lifetime runs on engine time, which keeps running while paused, so toasts expire in the pause menu too. Drawn at UI order 880: above the
  HUD, under the debugger (890), game menu (900) and pause menu (1000).
- Cost: no per-frame allocation (fixed ring of 16, each toast's width measured once, text textures cached by `core::UIHandler`); one glass
  panel + one rect + one text per visible toast. 4 visible toasts: +0.05 ms/frame of UI time on the dev PC (`--profile`: 0.126 -> 0.181 ms).

## Config (`config/game.json`)
| key | default | |
|---|---|---|
| `toast.enabled` | `true` | off = no service provided |
| `toast.max_visible` | `4` | 1..16 |
| `toast.seconds` | `4.0` | default lifetime (0.5 .. 60) |

## Dev flag
`--toast-test` shows one Info toast at startup; `--toast-test=info,warning:Cargo nearly full,urgent:HULL BREACH` shows several
(`level[:text]`, comma separated). Test toasts last 600 s so any `--screenshot-frame` catches them:
```
SDL_AUDIODRIVER=dummy ./space_game_v2 --display=1 --input-profile=testing --frames=130 --screenshot-frame=120 \
  "--toast-test=info:Recipe learned,warning:Cargo nearly full,urgent:HULL BREACH" --screenshot=logs/toast_stack.bmp
```

## Code
`toast_rules.h` (pure: `Stack` queue state machine, `fadeAlpha`, `flash`, `levelColor`, `slotX/slotY`) is unit-tested in
`package/tests/test_toast.cpp`; `toast.cpp` is the module (config, service, panel, dev flag).
