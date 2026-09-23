# Space Game V2 - Module Guide

Everything (input, rendering, UI, asset loading, gameplay) is a **module**. The engine in `package/engine/`
only knows how to find modules, order them, and call their hooks. Build: C++23, `make`.

```
package/engine/            kernel: Module interface, EventBus, Services, main loop  (rarely touched)
package/modules/core/      window, input_handler, render_engine, ui_handler, import_handler
package/modules/ship/      the ship: ship_core (IShip), cockpit, warp_drive, orbit_lock, docking (IDocking), respawn, fake_ship (dev)
package/modules/ui/        main_menu (title screen before gameplay: docs/MAIN_MENU.md), pause_menu, ship_hud
package/modules/combat/    weapons (blaster + mining beam, ICombat)
package/modules/core/input_methods/  keyboard, mouse, joystick (joysticks / wheels / pedals / shifters: docs/CONTROLLERS.md)
package/modules/gameplay/  inventory (IInventory: the cargo hold), mining (ore chunks from destroyed asteroids), crafting (ICrafting), blueprints (IBlueprints: recipe unlocks, docs/BLUEPRINTS.md)
package/modules/ui/game_menu  the tabbed game menu (IGameMenu)
package/modules/ui/system_map  the MAP tab of the game menu (star system schematic, docs/SYSTEM_MAP.md)
package/modules/fx/        particles (engine exhaust, sparks, debris, warp flash; fx::SpawnParticles event)
package/modules/world/     starfield, skybox, star_system (IStarSystem), asteroids (IAsteroids), stations (IStations), anomalies (IAnomalies), atmosphere (planet rim glow, visual only: docs/ATMOSPHERE.md)
package/template/          scaffold used by package/tools/new_module.sh
```

## Add a module (2 steps)
1. `package/tools/new_module.sh gameplay weapons` (creates `package/modules/gameplay/weapons/weapons.cpp`)
2. `make run` - no other file needs editing.

Or by hand: drop any folder with a `.cpp` containing `REGISTER_MODULE(YourClass);` under `package/modules/`.

## Change or remove a module
- Replace: edit or swap the folder. Other modules only depend on the module's **name** and its
  public header, so an alternative implementation can take its place.
- Disable at build time: prefix the folder or file with `_` (`package/modules/ship/_cockpit/`).
- Disable at run time: `./space_game_v2 --disable=ship/cockpit,core/ui_handler`
- See what loaded: `make modules` (or `--list-modules`).

## The Module interface (`engine/module.h`)
| Hook | When |
|---|---|
| `name()` | unique id, `category/name` |
| `dependencies()` | names that must init first; missing/failed dep => module skipped |
| `optionalDependencies()` | init after these *if they exist*; absence is fine (for optional services) |
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
| `core::ISaveSystem` | `registerSaveable(this)` (implement `ISaveable`), `saveSlot/loadSlot` - see docs/SAVES.md |
| `core::IAudio` | `play("name")`, `playLoop` / `setLoopVolume` - see docs/AUDIO.md |
| `core::IData` | game content by id: `get("ores", "iron")`, `ids("recipes")` - see docs/DATA.md |
| `core::ICamera` / `ITransformSource` | the view: publish a pose with `ITransformSource::transform(alpha)`; camera modes (V) |
| `core::IPhysics` | sphere bodies + `Collided` events: `addBody`, `setBody`, `teleport` - see docs/PHYSICS.md |
| `ship::IShip` | the ship's status and actions: `status()`, `applyDamage`, `consumeWarpFuel`, `kill`, `respawn`; events `DamageTaken`, `Died`... - see docs/SHIP.md |
| `world::IStarSystem` | the sun, planets and moons: `bodies()` (double positions), `sunPosition()`, `positionAt(id)`, `simTime()` - see docs/WORLD.md |
| `world::IAsteroids` | the asteroid field, read-only: `count()`, `position(i)`, `radius(i)`, `ore(i)`, `nearest(point, n, out)` - see docs/WORLD.md |
| `world::IStations` | the space stations: `count()`, `info(i)` (position/velocity in double, dock radius, up), `nearest(p)` - see docs/STATIONS.md |
| `world::IAnomalies` | anomaly sites: `count()`, `position(i)`, `detected(i)`, `investigated(i)`; event `world::AnomalyInvestigated` - see docs/ANOMALIES.md |
| `ship::IDocking` (+ events `Docked`, `Undocked`) | `docked()`, `stationName()`, `nearestDockable(...)`: what a "DOCK [G]" prompt needs - see docs/STATIONS.md |
| `core::IControllers` | the connected joysticks / wheels: `devices()`, `deviceCount()`, `rawAxis(dev, i)`, `rawButton`, `rawHat`, `lastEvent()` - see docs/CONTROLLERS.md |
| `ui::IMainMenu` (+ event `GameStarted`) | `isOpen()`: the startup main menu is up, the engine is held paused and no game has started - see docs/MAIN_MENU.md |
| `ui::IPauseMenu` | `openSettingsOnly()`, `settingsOnlyOpen()`: the pause menu's Settings page as a stand-alone screen (used by the main menu) |
| `ui::IGameMenu` | `addTab(name, order, drawFn)`, `removeTab`, `open()`, `close()`, `isOpen()`: the game menu on the I key; other modules add tabs - see docs/GAME_MENU.md |
| `gameplay::ICrafting` (+ events `CraftResult`, `ItemUsed`) | `recipes()`, `canCraft`, `craft`, `use`: turn ore into items and use them - see docs/CRAFTING.md |
| `gameplay::IBlueprints` (+ event `BlueprintUnlocked`) | `unlocked(id)`, `unlock(id)`, `list()`, `displayName(id)`: recipe unlocks from exploration; absent = no gate - see docs/BLUEPRINTS.md |
| `gameplay::IInventory` (+ events `InventoryChanged`, `CargoFull`, `OreMined`) | the cargo hold: `capacity()`, `used()`, `free()`, `count(id)`, `add(id, n)` (partial accept), `remove(id, n)` (atomic), `stacks()`, upgrade levels - see docs/INVENTORY.md, docs/MINING.md |
| `combat::ICombat` (+ events `ProjectileHit`, `WeaponChanged`, `Overheated`) | selected weapon, heat, overheat, firing, hit-marker age: what a crosshair heat bar needs - see docs/COMBAT.md |
| `fx::SpawnParticles` (event) | emit it to spawn a particle burst: `{kind, position, direction, velocity, count, ...}` - see docs/FX.md |
| `ship::OrbitLockChanged` (event) | `{locked, bodyName}` when ship/orbit_lock engages or releases - see docs/ORBIT_LOCK.md |
| `ship::IRespawn` | `counting()` / `secondsLeft()` of the respawn countdown (HUD uses it) - see docs/RESPAWN.md |
| `cockpit::ICockpitScreens` | put content on the cockpit model's `@` screens: `registerRenderer(group, fn)`; `showsDefaultUI()` - see docs/COCKPIT.md |
| `core::ISettings` | player prefs: `get("video.fov", 90.0f)`, `set(...)`, `SettingChanged` event - see docs/CONFIG.md |
| `core::IQuality` | the graphics preset in use (`activeName()`, `selectedName()`); set defaults for tunables, see docs/QUALITY.md |
| `core::UIHandler` | `addPanel`, glass panels, `button/toggle/slider`, `text` - see docs/UI.md |
| `core::IDebug` | the in-game debugger (F6-F8, module `core/debugger`): `watch(name, getter)`, `drawHook(name, fn)`, `logEvent(line)`, `unwatch` / `removeDrawHook` in shutdown - see docs/DEBUGGER.md |
| `core::IToast` | generic notification popups (module `ui/toast`, bottom-right, Info / Warning / Urgent): `show(text, level, seconds)`, `clear()` - optional, see docs/TOAST.md |
| `core::ImportHandler` | `load<Mesh/Texture/TextAsset>("models/x.obj")` from `assets/`, cached; `registerLoader(".ext", fn)` adds a format |

`package/modules/ship/ship_core/ship_core.cpp` is the reference: provides `ship::IShip`, registers render passes, is saveable, uses physics/audio,
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
