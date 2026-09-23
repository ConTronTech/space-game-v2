# Quick Action Bar (`ui/quick_action_bar`, B key)

A No Man's Sky-style quick menu for flight: reach for a secondary action without pausing or leaving flight. Redesigned 2026-09-23 (user
feedback after the first version) from a single flat row into a grid of category ROWS, with mouse support and WASD reuse. This section is
the design spec, **built 2026-09-23** - see "As built (2026-09-23, category rows)" below for the specifics; the "As built, original flat-row
version" section further down is the first build's reference (still accurate for entry contents/tunables, only layout and navigation changed).

## Why it doesn't pause flight (explicit user decision, 2026-09-23)
This is a genuinely LIVE menu: the ship keeps flying while it's open, on purpose - "it's the user's decision to be quick." That means input
keys can be **reused** between flight and menu navigation rather than the menu suspending flight controls. The general mechanism (works for
any future system, not just this one): `core::IInput`'s binding table maps *action names* to keys, and nothing stops two different action
names from both listing the same physical key - each action reads that key's state independently every frame, with no conflict or
suppression. So `quick_bar_up`/`down`/`left`/`right` can bind to the SAME keys `thrust`/`strafe`/`lift` already use: pressing W fires both
`thrust` (ship moves) and `quick_bar_up` (selection moves) in the same frame, genuinely simultaneous. Using the bar costs you some flying
precision, on purpose - that's the tradeoff of it being fast instead of safe. (If a future system ever needs to genuinely STEAL a key away
from another action instead of sharing it, `IInput::consume()` is the tool for that - not the default behavior, and not needed here.)

## Layout: rows of categories, like NMS's quick menu
Not a single flat strip - a small grid, one ROW per category, entries within a row scrollable/selectable individually:
- **WEAPONS** row: one entry per weapon slot with an input action (`weapon_1..3`), same as today's flat version.
- **SHIP SYSTEMS** row: WARP (toggle), ORBIT LOCK (toggle), and a slot for whatever ship-level toggles get added later (shield enable/disable
  once that exists, future ship modules) - this row is explicitly where new systems should register their quick-bar entries as the game grows.
- **ITEMS / PERKS** row: Ore Scanner / Anomaly Scanner (today: shown greyed, "PASSIVE - ALWAYS ON", since neither has an active-use action
  yet - keep that behavior, don't fake activation), and a slot for future consumables/perk items.
- Category rows are **data-driven from the same live-service rebuild the flat version already does** - a category with zero live entries
  simply doesn't draw a row that frame, same spirit as the original "only what's actually available shows up."

## Navigation
- **WASD reused directly** (see above): W/S move the selection up/down BETWEEN rows, A/D move LEFT/RIGHT WITHIN the current row. This is
  in ADDITION to the existing flight bindings on those same keys - both fire.
- `quick_bar_confirm` (`Return`) activates the selected entry, same as today.
- **Mouse**: hover a row/entry to highlight it (same hover pattern the CARGO grid uses - `core::UIHandler::hovered`), click to activate it
  directly (skips needing to move the WASD selection there first). Mouse and WASD selection should stay in sync (hovering updates `selected`,
  same as the CARGO grid's mouse+keyboard coexistence).
- `quick_bar_open` (`B`) still opens/closes the whole bar; `quick_bar_prev`/`next` (`,`/`.`) can stay as a secondary same-row scroll for
  controller/wheel use (no WASD-equivalent hat mapping yet) - don't remove them, just add WASD/mouse alongside.

## What stays the same from the original build
Everything else about the original flat-row version carries over unchanged: entries rebuilt from live services every frame (not cached),
instant vs toggle entry kinds with per-entry availability/cooldown, activating an entry injects the target module's own input action for
that frame rather than calling into it directly (so warp's fuel check, orbit lock's alignment refusal, etc. all still go through their
normal code path), the idle timeout auto-close, and drawing via `core::UIHandler` panels (order 850).

## As built (2026-09-23, category rows)
- **Model** (`quick_bar_rules.h`): `struct Row { name; entries; }`; `State` holds `row`/`col` (plus `selectedId`, which the selection
  follows across rebuilds). `step()`/`resync()` take `std::vector<Row>`. The module's `rebuild()` fills WEAPONS, SHIP SYSTEMS and
  ITEMS / PERKS in that fixed order and drops any row that ends up empty. The rules also skip empty rows, as a safety net.
- **Controls** (in both `config/input/default.json` and `testing.json`):

  | action | key | does |
  |---|---|---|
  | `quick_bar_up` / `quick_bar_down` | `W` / `S` (same keys as `thrust`) | previous / next row, wrapping last <-> first |
  | `quick_bar_left` / `quick_bar_right` | `A` / `D` (same keys as `strafe`) | previous / next entry in the current row, wrapping within the row |
  | `quick_bar_prev` / `quick_bar_next` | `,` / `.` | same as left / right (same-row only, per the spec) |
  | `quick_bar_open`, `quick_bar_confirm`, `quick_bar_close` | `B`, `Return`, unbound | unchanged |

  Nothing is consumed: while the bar is open, W/S/A/D fly the ship and move the selection in the same frame. Up + down, left + right or
  prev + next pressed together cancel out.
- **Row change keeps the column where it fits.** Moving from column 2 into a two-entry row lands on that row's last entry. With only one
  non-empty row, up/down do nothing (no `Moved`, no click sound). The same goes for left/right in a one-entry row.
- **Priority within one frame:** open/close, mouse click, up/down, left/right (prev/next), mouse hover, confirm. At most one outcome per
  frame.
- **Mouse** (the CARGO-grid / main-menu pattern): `onFrameBegin` hit-tests the tile rects the bar drew last frame with `UIHandler::hovered`.
  Hover takes the selection **only on a frame the cursor actually moved** (`mouseX/Y != lastMx_/lastMy_`, as in `main_menu.cpp` /
  `pause_menu.cpp`). This lets W/S/A/D move away from a tile the cursor is parked on without being pulled back, and the keyboard wins a frame
  where both happen. A left-button press edge (`mouseHeld()` rising) over a tile selects it and activates it through the same
  availability/cooldown gate as `Return`. Hovering and clicking reset the idle timer.
- **The bar never changes mouse capture.** `hovered()` is false while the cursor is captured, so in the default profile (mouse look on), the
  mouse keeps steering and the bar is keyboard-only. Hover and click work once the cursor is free: `Tab` (`mouse_capture_toggle`), or a
  capture-off profile such as `testing.json`. Weapons refuse to fire while the cursor is free, so clicks on the bar never shoot. The footer
  adds "mouse: click to use" only while the cursor is free. Freeing the cursor on open, as the game menu does, was left out on purpose: it
  would stop mouse look, which counts as flight suppression. The fake-`PauseChanged` trick would also nest badly with the game and pause
  menus, which share the mouse module's single "captured before pause" memory.
- **Look:** a single glass panel at the bottom centre, with rows stacked vertically. A 118 px (scaled) label gutter on the left holds each
  row's name. The current row gets an accent bar and accent-coloured text. The tiles are unchanged from the flat version. Every row uses the
  same tile width so the columns line up; tiles shrink to fit the widest row on narrow windows. The panel grows upward from the same bottom
  edge as before, and the footer lists the live keys from `primaryBindingLabel`.
- **Controllers:** still no profile binds the bar. Once the SideWinder hat is identified, the natural D-pad mapping is
  `{"index": 0, "up": "quick_bar_up", "down": "quick_bar_down", "left": "quick_bar_left", "right": "quick_bar_right"}`, with open and
  confirm on buttons. `prev`/`next` alone cannot leave the current row.

## As built, original flat-row version (2026-09-22, superseded above by the 2026-09-23 redesign)
Kept for reference - the entry table, scanner/shield notes and tunables below are still accurate for what exists in each entry; only the
LAYOUT (flat strip vs. category rows) and navigation (keyboard-only vs. WASD + mouse) changed.

An in-flight quick menu in the spirit of No Man's Sky's Quick Menu: a strip of secondary actions near the bottom of the screen, opened
with one key, **without pausing the game** or leaving flight. It was ported in concept from the old game's `QuickActionBar`
(`include/ui/quickbar/quick_action_bar.h`) and planned in docs/NEXT_UP.md #10. It is meant for rigs with few spare buttons
(wheel + joystick): one open button, left/right and a confirm button reach everything in the bar.

Fire, thrust, steering and docking are **not** in the bar. They stay on their own always-live controls.

### Controls (config/input/default.json)
| action | default key | does |
|---|---|---|
| `quick_bar_open` | `B` | open the bar; press again to close (with `quick_bar.hold_to_open`: open while held, close on release) |
| `quick_bar_prev` | `,` | previous entry (wraps to the end) |
| `quick_bar_next` | `.` | next entry (wraps to the start) |
| `quick_bar_confirm` | `Return` | use the selected entry |
| `quick_bar_close` | (unbound) | optional explicit close, e.g. D-pad down on a controller |

`Return` is also `ui_confirm`, but only the pause menu reads that, and only while paused. The bar closes itself when the game pauses.

The bar also closes on its own after `quick_bar.timeout` seconds with no input (5 s, the old game's `TIMEOUT_DURATION`). Any bar key
restarts that timer, and a thin accent line under the title shows the time left. The bar stands down while the pause menu, the game
menu (`I`) or the controller setup screen is open.

**Controllers:** no device profile binds the bar yet. The SideWinder's hat is still unidentified (`"hats": []` in
`config/input/devices/045e_003c_sidewinder.json`). Once it is identified, a D-pad layout like the old game's is one line:
`{"index": 0, "up": "quick_bar_open", "down": "quick_bar_close", "left": "quick_bar_prev", "right": "quick_bar_next"}`, plus a button
with `"action": "quick_bar_confirm"`.

### What is in the bar
The list is **data-driven**: while the bar is open it is rebuilt every frame from the services that exist, so only what the ship actually
has appears. A module that is not loaded simply contributes nothing.

| entry | kind | from | available when | activating it |
|---|---|---|---|---|
| one per weapon (BLASTER, MINING BEAM, MISSILES) | instant | `combat::ICombat` | always; the selected one shows SELECTED, missiles show their ammo | presses `weapon_1`..`weapon_3` |
| WARP | toggle (ON/OFF) | `ship::IWarpDrive` | engaged, or alive with `warp.min_fuel` fuel (the same rule as `Z`) | presses `toggle_warp` |
| ORBIT LOCK | toggle (ON/OFF) | `ship::IOrbitGuide` + `OrbitLockChanged` | locked, or the orbit guide is active (a body in range, not docked/warping/dead); the detail line shows the body or the refusal reason | presses `toggle_orbit_lock` |
| ORE SCANNER / ANOMALY SCANNER | status only | `gameplay::IInventory::hasPerk` | never: shown greyed as "PASSIVE - ALWAYS ON" once installed | nothing |

**How an entry fires.** The bar does not call into the target module. It injects that module's own input action for one frame
(`IInput::contribute`, from its `onFrameBegin`, right after the input poll), so the target sees an ordinary key press. The bar is only
another way to reach the same code path as the dedicated key: warp's fuel check, orbit lock's alignment refusal, `WeaponChanged`
and the logging all behave exactly as if `Z`/`O`/`1` had been pressed. A new entry needs no API change in the target module, as long
as that module already has an input action.

**Scanners are status entries, not buttons.** Both scanner perks are passive today. Using the crafted item installs a permanent perk,
and from then on ore labels or anomaly radar dots are always on. There is nothing to "activate", so the bar lists them (once installed)
but never pretends to fire them. If an active scan pulse is added later, give it an input action and add it as an instant entry with a
cooldown.

**Not in the bar yet:** shield on/off (the ship contract has no enable/disable call; `installShield` refills the shield, so it cannot
double as a toggle) and the beacon/distress-signal scan - **update (2026-09-23): distress beacons exist now** (`world/distress_beacons`),
so an ITEMS/PERKS-row entry for it is a reasonable follow-up once the redesign lands.

### Look (original)
The bar was a glass strip at the bottom centre, with a dark backing so it stays readable over a bright planet. Each entry was a glass tile
with its label, a small detail line and, for toggles, a green ON or red OFF. The selected tile got an accent frame. An unavailable tile
was dimmed and its label greyed. A tile on cooldown was covered by a dark fill that shrinks as the cooldown runs down. The footer showed the
live key labels (`IInput::primaryBindingLabel`). On narrow windows or with many entries the tiles shrank to fit.

### Tunables (config)
| key | default | meaning |
|---|---|---|
| `quick_bar.timeout` | 5.0 | idle seconds before the bar closes; 0 = never |
| `quick_bar.hold_to_open` | false | true: open only while `quick_bar_open` is held |
| `quick_bar.close_on_use` | false | close right after an entry fires |
| `quick_bar.toggle_cooldown` | 0.5 | seconds a toggle (warp, orbit lock) is locked after use, so a double tap cannot flip it straight back |

### Code
- `package/modules/ui/quick_action_bar/quick_bar_rules.h`: pure rules, unit-tested in `package/tests/test_quick_action_bar.cpp`.
  `step()` handles one frame of input (open/close, row and in-row wrap-around selection, mouse hover/click, confirm gated by `available`
  and the cooldown). `tick()` runs cooldowns down (also while the bar is closed) and the idle timer. `resync()` keeps the selection on the
  same entry id when the rows are rebuilt. Cooldowns are keyed by id for the same reason.
- `package/modules/ui/quick_action_bar/quick_action_bar.cpp`: the module (entry building, input injection, drawing with
  `core::UIHandler` at panel order 850, under the game menu and pause menu).
