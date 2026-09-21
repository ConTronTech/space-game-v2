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

## Perks
Permanent flags bought with items, kept in the inventory's save (`"perks": ["ore_scanner"]`; an older save without the key = no perks):
`IInventory::hasPerk(id)` and `addPerk(id)` (false if already set). The only perk so far is **`ore_scanner`** (set by using the Ore Scanner item, gameplay/crafting);
the HUD / radar reads `hasPerk("ore_scanner")` to show ore labels (not implemented yet). The set is pure (`gameplay::Perks` in `inventory_rules.h`).

## Saving
Save id `gameplay/inventory`: `{"level": 0, "stacks": [{"id": "iron", "amount": 50}, {"id": "copper", "amount": 20}]}`. Loading replaces the hold; bad entries (empty id, amount <= 0) are dropped, duplicates merged; a missing `stacks` key gives an empty hold.

## Adding an item
Put it in `data/items.json` (name, description, colour, `effect`, optional `volume`); nothing else is needed for it to be carried. Crafting recipes (`data/recipes.json`) come with 5.5.

## Tunables and dev flags
`inventory.capacity` 100, `inventory.upgrade_level` 0. **Test-only** flags: `--give=iron:50,copper:20` adds cargo at startup (logged as a warning; a bare id gives 1); `--clear-cargo` empties the hold at startup. At debug log level a cargo summary is logged every 5 s.

## Cargo rework: per-resource holds + general hold (5.1c)
- Every ore in `data/ores.json` has its OWN hold, capped by the optional `cargo_cap` field (default 20; iron 100, copper 80, titanium 60, cobalt 50, gold 50, platinum 10, uranium 10, crystal 10; filler `rock` 100, not counted in totals). Filling one ore never blocks another.
- Crafted items live in a GENERAL hold: `inventory.general_capacity` (default 30) units, each item taking its `volume` (items.json, default 1).
- Upgrade levels (`data/cargo.json`) carry `resource_mult` and `general_mult` (1/2/4/8). The old `capacity_mult` is still read as a fallback for both.
- API: `capacity(id)`, `free(id)`, `isResource(id)`, `generalCapacity/Used()`; `used()/capacity()/free()` are now the TOTALS (counted ore holds + general hold; base 400).
- `add()` accepts what fits per pool and emits `CargoFull{id, refused}` when something is refused. `remove()` is atomic.
- Softlock audit: (1) one ore filling a shared hold blocked everything -> per-resource pools; (2) general hold full of unusable items (Missile Pack, Ore Scanner) -> CARGO tab discard buttons; (3) full general hold refusing crafts -> discard/use; (4) warp fuel 0 is not a softlock (thrust, mining work, respawn refills); (5) capsule with a full ore hold stays red and is retried when room appears; (6) every recipe ingredient need <= its cap and every item volume <= general capacity, guarded by a unit-test lint; (7) old overfull saves are clipped with a warning, never crash.
