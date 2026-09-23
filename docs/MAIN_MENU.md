# Main menu (`ui/main_menu`)

The title screen between the boot loading bar (`core/boot_screen`) and gameplay. Code: `package/modules/ui/main_menu/`
(`main_menu.cpp`, pure logic in `main_menu_rules.h`, tested by `package/tests/test_main_menu.cpp`; service in `main_menu_api.h`).

## Flow
```
boot_screen (modules load, world generated, ship spawned)  ->  MAIN MENU (engine held paused)  ->  gameplay
                                                               |- Continue   loadSlot(newest save on disk) then unpause
                                                               |- New Game   unpause (the boot world is the new game)
                                                               |- Load Game  pick a slot -> loadSlot -> unpause
                                                               |- Settings   the pause menu's Settings page (Back returns here)
                                                               '- Quit
```
* **Continue** is listed only when a save exists. It loads `mainmenu::continueSlot(activeSlot(), listSlots())`: the active
  slot if one is set and still on disk, otherwise the newest file (`listSlots()` is newest first, the autosave counts).
  `activeSlot()` is session state and is always empty on a fresh launch, so in practice Continue = "newest save on disk".
  The line under the title shows which save that is.
* **Load Game** lists up to 6 saves, newest first (the pause menu's list). A failed load leaves the boot world untouched and
  shows an error line.
* **Esc** goes back from Load; it does nothing on the main page (there is no game to resume).
* Mouse, or the `ui_up` / `ui_down` / `ui_confirm` / `pause` actions (keyboard, controller, `core/input_methods/agent`).

## How gameplay is gated (no engine changes)
Every module still initialises exactly as before; there is no "no session yet" phase in `engine::Engine`. Instead the menu
holds the engine **paused** (`Engine::setPaused(true)`), the same freeze the pause menu uses and every gameplay module
already respects:
* no `onFixedUpdate`: no physics, gravity, star-system motion, autosave timer, respawn countdown;
* modules that act in `onUpdate` already check `paused()`: weapons, docking, warp, orbit lock, particles, gravity, the game
  menu (won't open), the quality auto-check; the mouse is released (`PauseChanged`), the engine hum is muted.

On top of that:
1. `onFrameBegin` re-asserts the pause every frame while the menu is open (before fixed update), so nothing else can
   unpause the world underneath it (e.g. the controller-setup screen releasing a pause it thought it owned).
2. `ui::IMainMenu::isOpen()` makes `ui/pause_menu` invisible and deaf to Esc while the main menu is up.
3. An opaque backdrop (UI panel order 990: above HUD/toast/debugger/profiler/game menu, below the pause menu's 1000 and
   controller setup's 1100) hides the frozen boot world and its HUD.

Leaving the menu = `setPaused(false)` (after `loadSlot` for Continue / Load) and the event `ui::GameStarted{how, slot}`. When
the menu is skipped, `GameStarted{Skipped}` is emitted on the first frame instead, so a subscriber always gets exactly one.

## Settings: borrowed, not duplicated
`ui/pause_menu` provides `ui::IPauseMenu` (`pause_menu_api.h`): `openSettingsOnly()` shows just its Settings page, and Back /
Esc closes it instead of falling through to the PAUSED page. The main menu keeps drawing only its backdrop meanwhile.
Input hand-off between the two: the main menu ignores all input on a frame that *began* with the borrowed page (or the
Controllers screen) open, and applies its own mouse clicks at the next `onUpdate` instead of inside the draw, so the one
Enter/Esc/click that closes or opens the Settings page never also hits a button on the other screen.

## When the menu is skipped
`mainmenu::shouldShow`: `--no-main-menu` always skips; `--main-menu` always shows (even with automation flags, e.g. to
screenshot it: `--main-menu --frames=60 --screenshot=...`); otherwise `main_menu.enabled` (default true) unless an
automation flag is present: `--frames`, `--benchmark`, `--paused`, `--screenshot`, `--ui-click`, `--open-menu`,
`--gravity-scenario`, `--fx-test-loop`, `--fx-test-thrust`, `--toast-test`, `--auto-fire`, `--auto-gravity`, `--auto-lock`,
`--auto-weapon` (`kAutomationFlags`). So `package/tools/smoke.sh` and the benchmark behave exactly as before.
Other dev flags that just set up the boot world (`--ship-start`, `--give`, ...) keep the menu: pick New Game to use them
(Continue / Load would replace that state with the save).

## Tunables
`main_menu.enabled` (true).

## Known limits
* The menu only exists at startup: the pause menu's Exit Game still quits (no "back to main menu" yet, which would need a
  real world reset for New Game).
* New Game does not regenerate anything: it is the world generated at boot.
