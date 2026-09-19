# Space Game V2 - Module Guide

Everything (input, rendering, UI, asset loading, gameplay) is a **module**. The engine in `package/engine/`
only knows how to find modules, order them, and call their hooks. Build: C++23, `make`.

```
package/engine/            kernel: Module interface, EventBus, Services, main loop  (rarely touched)
package/modules/core/      window, input_handler, render_engine, ui_handler, import_handler
package/modules/gameplay/  game features (flight, weapons, ...)
package/template/          scaffold used by package/tools/new_module.sh
```

## Add a module (2 steps)
1. `package/tools/new_module.sh gameplay weapons` (creates `package/modules/gameplay/weapons/weapons.cpp`)
2. `make run` - no other file needs editing.

Or by hand: drop any folder with a `.cpp` containing `REGISTER_MODULE(YourClass);` under `package/modules/`.

## Change or remove a module
- Replace: edit or swap the folder. Other modules only depend on the module's **name** and its
  public header, so an alternative implementation can take its place.
- Disable at build time: prefix the folder or file with `_` (`package/modules/gameplay/_flight/`).
- Disable at run time: `./space_game_v2 --disable=gameplay/flight,core/ui_handler`
- See what loaded: `make modules` (or `--list-modules`).

## The Module interface (`engine/module.h`)
| Hook | When |
|---|---|
| `name()` | unique id, `category/name` |
| `dependencies()` | names that must init first; missing/failed dep => module skipped |
| `priority()` | lower runs earlier (init order and per-phase order) |
| `required()` | true => engine aborts if `init` fails |
| `init` / `shutdown` | lifecycle (shutdown runs in reverse order) |
| `onFrameBegin` | poll OS events, clear |
| `onFixedUpdate(dt)` | fixed 60 Hz physics, 0..N per frame |
| `onUpdate(dt)` | game logic, once per frame |
| `onRender` / `onRenderUI` | raw drawing (prefer passes/panels below) |
| `onFrameEnd` / `onPresent` | cleanup / swap buffers |

## How modules talk
- **Services** (`engine.services`): `provide<T>(this)` in init; others `require<T>()` / `get<T>()`.
- **Events** (`engine.events`): `subscribe<E>(fn)` / `emit(E{})`. Any struct is an event.
  Example: window emits `SdlEvent`, `WindowResized`; anything can emit `engine::QuitRequested`.

## Core services
| Service | Use |
|---|---|
| `core::IInput` | actions from JSON profiles: `value("thrust")`, `down/pressed/released("fire")` - see docs/INPUT.md |
| `core::RenderEngine` | `addPass(name, order, fn)`, `camera` (view matrix, fov) |
| `core::UIHandler` | `addPanel`, glass panels, `button/toggle/slider`, `text` - see docs/UI.md |
| `core::ImportHandler` | `load<Mesh/Texture/TextAsset>("models/x.obj")` from `assets/`, cached; `registerLoader(".ext", fn)` adds a format |

`package/modules/gameplay/flight/flight.cpp` is the reference: binds input, registers passes and a HUD panel,
publishes the camera. Copy its shape.

- **Config** (`engine.config`): base value in code + optional override in `config/game.json` - see docs/CONFIG.md.

## Logging
`#include "engine/log.h"` then `LOG_I("mytag", "loaded %d things", n);` (also `LOG_W`, `LOG_E`, `LOG_D`). Goes to the console and
`logs/game.log`. Set `engine.log_level` (debug/info/warn/error) in `config/game.json`. Don't use `printf`/`fprintf` in modules.

## Tests
`make test` builds and runs `package/tests/` (no window needed). Add a file `package/tests/test_<thing>.cpp`:
```cpp
#include "tests/test.h"
TEST(my_thing_does_x) { CHECK_EQ(1 + 1, 2); }
```
`make test-san` runs them under AddressSanitizer/UBSan. `package/tools/smoke.sh` runs both, a strict `-Werror` build and a short game run - use it before each commit.

## Rules of thumb
- **Depend on interfaces, provide interfaces.** A service is a small pure-virtual class (`core::IInput`) that the
  implementing module inherits and provides: `provide<IInput>(this)`. Consumers `require<IInput>()`, so the
  implementation can be swapped by dropping in another module that provides the same interface. New services
  (audio, save, ...) are born with their interface.
- Register things in `init`, undo them in `shutdown`.
- Talk to other modules through services/events, never by including their `.cpp` internals.
- Put a module's public API in `<name>.h` next to it; include as `"category/name/name.h"`.
- Keep game state inside your module, not in globals.
