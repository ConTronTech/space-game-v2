# Quick Action Bar (`ui/quick_action_bar`)

An in-flight quick menu in the spirit of No Man's Sky's Quick Menu: a strip of secondary actions near the bottom of the screen, opened
with one key, **without pausing the game** or leaving flight. It was ported in concept from the old game's `QuickActionBar`
(`include/ui/quickbar/quick_action_bar.h`) and planned in docs/NEXT_UP.md #10. It is meant for rigs with few spare buttons
(wheel + joystick): one open button, left/right and a confirm button reach everything in the bar.

Fire, thrust, steering and docking are **not** in the bar. They stay on their own always-live controls.

## Controls (config/input/default.json)
| action | default key | does |
|---|---|---|
| `quick_bar_open` | `B` | open the bar; press again to close (with `quick_bar.hold_to_open`: open while held, close on release) |
| `quick_bar_prev` | `,` | previous entry (wraps to the end) |
| `quick_bar_next` | `.` | next entry (wraps to the start) |
| `quick_bar_confirm` | `Return` | use the selected entry |
| `quick_bar_close` | (unbound) | optional explicit close, e.g. D-pad down on a controller |

None of these share a flight key: `Q` is roll, `A`/`D` strafe, `Space` lift, the mouse wheel is `weapon_next`. `Return` is also
`ui_confirm`, but only the pause menu reads that, and only while paused. The bar closes itself when the game pauses.

The bar also closes on its own after `quick_bar.timeout` seconds with no input (5 s, the old game's `TIMEOUT_DURATION`). Any bar key
restarts that timer, and a thin accent line under the title shows the time left. The bar stands down while the pause menu, the game
menu (`I`) or the controller setup screen is open.

**Controllers:** no device profile binds the bar yet. The SideWinder's hat is still unidentified (`"hats": []` in
`config/input/devices/045e_003c_sidewinder.json`). Once it is identified, a D-pad layout like the old game's is one line:
`{"index": 0, "up": "quick_bar_open", "down": "quick_bar_close", "left": "quick_bar_prev", "right": "quick_bar_next"}`, plus a button
with `"action": "quick_bar_confirm"`.

## What is in the bar
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
double as a toggle) and the beacon/distress-signal scan (the Phase 6 item does not exist yet).

## Look
The bar is a glass strip at the bottom centre, with a dark backing so it stays readable over a bright planet. Each entry is a glass tile
with its label, a small detail line and, for toggles, a green ON or red OFF. The selected tile gets an accent frame. An unavailable tile
is dimmed and its label greyed. A tile on cooldown is covered by a dark fill that shrinks as the cooldown runs down. The footer shows the
live key labels (`IInput::primaryBindingLabel`). On narrow windows or with many entries the tiles shrink to fit.

## Tunables (config)
| key | default | meaning |
|---|---|---|
| `quick_bar.timeout` | 5.0 | idle seconds before the bar closes; 0 = never |
| `quick_bar.hold_to_open` | false | true: open only while `quick_bar_open` is held |
| `quick_bar.close_on_use` | false | close right after an entry fires |
| `quick_bar.toggle_cooldown` | 0.5 | seconds a toggle (warp, orbit lock) is locked after use, so a double tap cannot flip it straight back |

## Code
- `package/modules/ui/quick_action_bar/quick_bar_rules.h`: pure rules, unit-tested in `package/tests/test_quick_action_bar.cpp`.
  `step()` handles one frame of input (open/close, wrap-around selection, confirm gated by `available` and the cooldown). `tick()`
  runs cooldowns down (also while the bar is closed) and the idle timer. `resync()` keeps the selection on the same entry id when the
  list is rebuilt. Cooldowns are keyed by id for the same reason.
- `package/modules/ui/quick_action_bar/quick_action_bar.cpp`: the module (entry building, input injection, drawing with
  `core::UIHandler` at panel order 850, under the game menu and pause menu).
