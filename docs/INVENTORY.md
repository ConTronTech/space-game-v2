# Inventory (`gameplay/inventory`)

The ship's **cargo hold**: a limited amount of ore and items, counted in cargo units. Fuel and shields are not free anywhere in the game: they are made from ore the player mines (docs/MINING.md) and, later, crafted (5.5, not built yet).
The same service is meant for NPC ships, so they play by the same rules. Code: `package/modules/gameplay/inventory/` (`inventory_rules.h` is pure and unit-tested by `test_inventory.cpp`).

## Model
* **Stacks** keyed by item id. Ores (`data/ores.json`) and items (`data/items.json`) share **one namespace**; an id nobody defined is simply a raw stack (allowed, so mods and tests need no registration).
* **Capacity** in cargo units: `inventory.capacity` (default 100) times the level's multiplier. Every unit of ore takes 1 cargo unit; an item takes its optional `"volume"` field from `data/items.json` (default 1, `0` = weightless, always fits).
* `add(id, amount)` returns how much **was accepted** (partial accept), never over capacity; `amount <= 0` or an empty id does nothing. `remove(id, amount)` is **all or nothing** (false and nothing changes if there is not enough); an emptied stack disappears.
* `stacks()` lists them in a stable order: the order the stacks were first created (adding to an existing stack keeps its place).
* After a load the hold may be over-full (saved with a bigger hold): the cargo is kept, `free()` is 0 until it is emptied or upgraded.

## Capacity levels (`data/cargo.json`)
Like the warp drive's levels: entries in order (level 0 first), each with `capacity_mult`. Shipped: 1 / 2 / 4 / 8 = **100 / 200 / 400 / 800** cargo units ("Standard hold", "Extended hold", "Cargo bay", "Freighter hold"). Built-in defaults when the file is missing.
`inventory.upgrade_level` picks the starting level; a saved game overrides it. Phase 5 crafting raises it with `IInventory::setLevel(n)` (clamped, immediate).

## Service and events (`inventory_api.h`)
`gameplay::IInventory`: `capacity()`, `used()`, `free()`, `count(id)`, `add(id, n)`, `remove(id, n)`, `stacks(out)`, `level()`, `levelCount()`, `setLevel(n)`.
Events: `gameplay::InventoryChanged{id, delta}` (positive = added, negative = removed) and `gameplay::CargoFull{id, refused}` (an add did not fully fit; `refused` units were turned away). Mining also emits `gameplay::OreMined{ore, amount}` (docs/MINING.md).
The HUD shows nothing yet: a cargo line comes with the HUD worker; everything it needs is above.

## Saving
Save id `gameplay/inventory`: `{"level": 0, "stacks": [{"id": "iron", "amount": 50}, {"id": "copper", "amount": 20}]}`. Loading replaces the hold; bad entries (empty id, amount <= 0) are dropped, duplicates merged; a missing `stacks` key gives an empty hold.

## Adding an item
Put it in `data/items.json` (name, description, colour, `effect`, optional `volume`); nothing else is needed for it to be carried. Crafting recipes (`data/recipes.json`) come with 5.5.

## Tunables and dev flags
`inventory.capacity` 100, `inventory.upgrade_level` 0. **Test-only** flags: `--give=iron:50,copper:20` adds cargo at startup (logged as a warning; a bare id gives 1); `--clear-cargo` empties the hold at startup. At debug log level a cargo summary is logged every 5 s.
