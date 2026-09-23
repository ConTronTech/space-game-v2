# Game menu (`ui/game_menu`)

A tabbed overlay on the **I** key: CARGO and CRAFTING today, more tabs later (map, ship, stats) added by the modules that own them. Code: `package/modules/ui/game_menu/` and the tabs widget in `core/ui_handler` (docs/UI.md).

## Controls
| | |
|---|---|
| **I** (`toggle_menu`) | open / close |
| **Esc** | close (while the menu is open it does NOT also open the pause menu) |
| mouse click on a tab label | switch tab |
| `ui_left` / `ui_right` (Left / Right, and A / D) | previous / next tab while open |
| `--open-menu=FRAME[,TAB]` (dev) | opens the menu on that frame, on tab number TAB (0 = the first) |

While the menu is open:
* the **mouse is free** and is put back the way it was on close (`engine::PauseChanged` is emitted, which the mouse module already handles: it remembers whether the cursor was captured, releases it, restores it on close and skips the first garbage mouse delta). Weapons refuse to fire while the mouse is not captured, so clicks in the menu never shoot.
* the **ship keeps flying** (the game is not paused): `menu.pause_game` (default false) makes it pause with `Engine::setPaused` and unpause on close instead. The menu does not open on top of the pause menu.
* The menu panel is centred, about 64% x 72% of the screen (at least 760 x 480), with a dark backing so the cockpit behind it does not fight the text. It is only drawn while open: no cost when closed.

**Esc handling:** the pause menu reads the `pause` action in its own `onUpdate`, and `IInput` has no way to consume an input. So `ui/game_menu` (which initialises after the input handler) adds -1 to the `pause` action for that frame right after the input handler has polled, turning the press into "no press" for everyone else, and keeps doing so while the key is held; the same key press is what closes the menu. The state machine is pure and tested (`menu_rules.h`); `pause_menu` was not changed. Using `IInput::contribute` (meant for devices) this way is a small hack: the clean version is a "handled" flag in `IInput` or a check in `pause_menu`.

## The tabs
* **CARGO** (built in): the hold as a list (colour square from `data/ores.json` / `items.json`, name, count), a capacity bar (green / yellow / red by fill), the ship's hull, shield and warp fuel bars, and a **USE** button on every usable item (through `gameplay::ICrafting::use`; the result line is shown at the bottom).
* **CRAFTING** (added by `gameplay/crafting`): docs/CRAFTING.md.
* **MAP** (order 30, added by `ui/system_map`): a flat top-down schematic of the whole star system, docs/SYSTEM_MAP.md.

## Adding a tab
```cpp
#include "ui/game_menu/game_menu_api.h"
if (auto* menu = eng.services.get<ui::IGameMenu>())          // optional: list "ui/game_menu" in optionalDependencies() so it exists first
    menu->addTab("MAP", 30, [this](core::UIHandler& ui, float x, float y, float w, float h) {
        ui.text(x, y, "hello", 16, ui.theme.text);            // draw inside the (x, y, w, h) content rectangle
        if (ui.button("Do it", x, y + 30, 120, 34, false)) { ... }
    });
// shutdown: menu->removeTab("MAP");
```
Tabs are sorted by `order` (CARGO 10, CRAFTING 20, MAP 30); a tab with an existing name replaces it. The draw function runs only while the menu is open and that tab is selected. `IGameMenu` also has `open()`, `close()` and `isOpen()` (a HUD hint, a station "trade" button...).
Do not list `ui/game_menu` and a module that adds tabs as optional dependencies of each other (an optional cycle leaves the order arbitrary and the tab never registers).

## Tunables
`menu.pause_game` (false).

## CARGO tab (unified slot grid, docs/INVENTORY.md)
Header "CARGO used / capacity slots" with the hold level name and a fill bar. Left: the cargo GRID (8 x 12 by default), one stack per slot: a tinted square, a 2-letter tag, the amount and a thin fill line (amount / stack cap). Right: an info panel (the hovered stack as a tooltip, else the selected slot: name, ore/item, description, this slot / total / stack size and the next level's stack size) with USE (usable items), DISCARD 1 and DISCARD STACK buttons, and a TRASH box below it.
Mouse: press a stack to select it; drag it onto another slot (empty = move, same id = merge up to the cap, other id = swap) or onto TRASH (discard the whole stack).
Keyboard / wheel D-pad / joystick: `ui_up/down/left/right` move the selection (clamped at the edges), `menu_discard` (Delete / Backspace, PXN left paddle) pressed TWICE on the same slot within 3 s discards it (the first press arms it and the outline turns red). Note: ui_* share W/A/S/D with flight, so flying with the menu open also moves the cursor (harmless: discarding always needs the two presses).
Discarding is always possible, so a full hold can never lock the player out. A message line shows the last discard or craft/use result.
