# Inventory (`gameplay/inventory`)

The ship's **cargo hold**: one unified GRID of slots that holds both the ore the player mines (docs/MINING.md) and the items they craft (docs/CRAFTING.md). Fuel and shields are not free anywhere in the game: they are made from mined ore.
The same service is meant for NPC ships, so they play by the same rules. Code: `package/modules/gameplay/inventory/` (`inventory_rules.h` is pure and unit-tested by `test_inventory.cpp`).

## Model: a unified slot grid (2026-09-22 rework, docs/NEXT_UP.md #7)
* **One grid**, `inventory.grid_columns` x `inventory.grid_rows` = **8 x 12 = 96 slots** by default. Ores and items share it; there is no separate ore pool / general pool any more.
* **One stack per slot.** Each id has a **stack cap** = its level-0 base x the hold level's `stack_mult`:
  * ores: `cargo_cap` in `data/ores.json` (iron 100, copper 80, titanium 60, gold 50, cobalt 50, uranium / platinum / crystal 10; missing = 20; the filler `rock` 100);
  * items: optional `stack_cap` in `data/items.json`, else 10 / `volume` (at least 1; no volume = 10). An id nobody defined is a raw item stack of 10 (allowed, so mods and tests need no registration).
  * A positive base never scales below 1; a multiplier <= 0 (or a base of 0) means that id cannot be carried.
* **The slot count never changes with the hold level; only the stack caps grow.**
* `add(id, n)` **auto-stacks**: it tops up the id's existing slots first (slot order, up to the cap), then spills into empty slots (slot order). It returns how much was accepted (partial accept); a grid with no matching room and no empty slot refuses (the module emits `CargoFull`). `n <= 0` or an empty id does nothing.
* `remove(id, n)` is **all or nothing** across every slot of that id, taken from the LAST slots first (so full stacks stay together); an emptied slot becomes empty.
* A **lowered** level (or an over-full saved slot) keeps its contents: the slot is simply over its cap and takes nothing more until it is emptied.

## Capacity levels (`data/cargo.json`)
Entries in order (level 0 first), each with `stack_mult`. Shipped: **1 / 2 / 4 / 6** ("Standard hold", "Extended hold", "Cargo bay", "Freighter hold"), so uranium stacks 10 / 20 / 40 / 60 and iron 100 / 200 / 400 / 600 per slot. Older files without `stack_mult` fall back to `resource_mult`, then `capacity_mult`. Built-in defaults when the file is missing.
`inventory.upgrade_level` picks the starting level; a saved game overrides it. Crafting raises it with `IInventory::setLevel(n)` (clamped, immediate).

## Service and events (`inventory_api.h`)
`gameplay::IInventory`, the old surface kept working on top of the grid:
* `capacity()`, `used()`, `free()`: the TOTALS, now in **slots** (slot count / occupied / empty). The HUD line reads "CARGO 12 / 96".
* `capacity(id)` = `count(id) + free(id)`; `free(id)` = how many more units of that id fit right now (room in its partial slots + every empty slot at a full stack). Mining uses `free(id) < 1` to leave a capsule red.
* `isResource(id)` (an ore, or rock), `resources(out)` (ore ids, data order, no rock), `count(id)` (summed over slots), `add`, `remove`, `stacks(out)` (one summed entry per id, in the order ids first appear in the grid), `level()`, `levelCount()`, `setLevel(n)`, `hasPerk`, `addPerk`.
* Removed: `generalCapacity()` / `generalUsed()` (there is no general hold; the old CARGO tab was the only caller).

New, for the grid UI and crafting:
* `slotCount()`, `gridColumns()`, `slots(out)` (every slot in grid order; an empty slot has an empty id), `stackCap(id)`, `stackCapAtLevel(id, level)`;
* `discardSlot(slot, amount)` (amount <= 0 = the whole slot; returns the units removed, emits `InventoryChanged`);
* `moveSlot(from, to)`: onto an empty slot = move, onto the same id = merge up to the cap (the rest stays behind), onto another id = swap;
* `roomAfter(removed, id)`: the room for `id` once the `removed` stacks are taken out, without changing anything. Crafting uses it: the result needs room for 1 unit AFTER its ingredients leave (an ingredient that empties a slot frees it).

Events: `gameplay::InventoryChanged{id, delta}` (positive = added, negative = removed; slot moves emit nothing, the totals do not change) and `gameplay::CargoFull{id, refused}`. Mining also emits `gameplay::OreMined{ore, amount}` (docs/MINING.md).

## CARGO grid (the I menu, docs/GAME_MENU.md)
The grid on the left, an info panel (tooltip on hover, else the selected slot) and a TRASH box on the right. Mouse: drag a stack onto another slot (move / merge / swap) or onto TRASH (discard). Keyboard / D-pad: `ui_up/down/left/right` select a slot, `menu_discard` (Delete / Backspace; PXN left paddle) twice on the same slot discards it. The panel also has USE, DISCARD 1 and DISCARD STACK buttons. Discarding is always possible, so a full grid can never softlock crafting or mining.

## Perks
Permanent flags bought with items, kept in the inventory's save (`"perks": ["ore_scanner"]`; an older save without the key = no perks):
`IInventory::hasPerk(id)` and `addPerk(id)` (false if already set). Perks so far: **`ore_scanner`** (the Ore Scanner item: ore type and yield on a locked rock, ore-coloured radar dots, docs/MINING.md "Ore Scanner") and **`anomaly_scanner`** (docs/ANOMALIES.md). The set is pure (`gameplay::Perks` in `inventory_rules.h`).

## Saving
Save id `gameplay/inventory`, **version 3**:
`{"version": 3, "level": 0, "slots": [{"slot": 0, "id": "iron", "amount": 50}, ...], "stacks": [{"id": "iron", "amount": 50}, ...], "perks": [...]}`.
* `slots` keeps the **layout the player arranged**: each stack goes back into its own slot. A bad index, a clash, or a stack over its cap is not lost: the leftover is auto-stacked into the grid afterwards.
* `stacks` (per-id totals) is written too, so an older build can still read the cargo.
* A version 1 / 2 save (no `slots` key) is **auto-stacked** into the grid in its stack order.
* Anything that still does not fit is clipped with a logged warning per stack; bad entries (empty id, amount <= 0) are dropped; missing keys = an empty hold. It never crashes.

## Adding an item
Put it in `data/items.json` (name, description, colour, `effect`, optional `stack_cap`); nothing else is needed for it to be carried. Recipes live in `data/recipes.json`.

## Tunables and dev flags
`inventory.grid_columns` 8, `inventory.grid_rows` 12 (each clamped 1..32), `inventory.upgrade_level` 0. **Test-only** flags: `--give=iron:50,copper:20` adds cargo at startup (logged as a warning; a bare id gives 1); `--clear-cargo` empties the hold at startup. At debug log level a cargo summary is logged every 5 s.

## Softlock audit (carried over from 5.1c, re-checked for the grid)
(1) One ore can still fill many slots, but every stack is discardable from the grid (drag to TRASH, buttons, or the two-press key). (2) Unusable items (Missile Pack, Ore Scanner) are discardable the same way. (3) A full grid refusing a craft is fixed by discard/use; crafting counts an ingredient that empties a slot as room for the result (`roomAfter`). (4) Warp fuel 0 is not a softlock. (5) A capsule refused by a full grid stays red and is retried when room appears. (6) A unit-test lint checks every recipe's ingredients fit an empty level-0 grid and every result can be carried. (7) Old saves are auto-stacked and clipped with a warning, never crash.

## History
5.1c (per-resource holds + a 30-unit general hold by volume, `resource_mult` / `general_mult`, save v2) was replaced by the unified grid on 2026-09-22 (docs/NEXT_UP.md #7).
