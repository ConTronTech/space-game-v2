# Data (game content as JSON)

Content lives in `data/`, not in code. `core/data_registry` provides `core::IData`.

```cpp
auto& data = eng.services.require<core::IData>();
for (auto& id : data.ids("ores")) {                    // definition order
    const engine::Json& ore = data.get("ores", id);    // null Json if missing: chaining is always safe
    float rarity = (float)ore["rarity"].num(1.0);
}
data.has("items", "repair_kit");
```

## Layout
- Each **folder** (`data/ores/*.json`) or **file** (`data/ores.json`) is a *category*.
- A file is an object of `"id": { ...definition... }`. Keys starting with `_` are comments.
- Files load alphabetically and a **later file replaces** an earlier definition with the same id
  (replaced whole, not merged). So `data/ores/zz_mymod.json` can add ores or change existing ones without touching the base files.
- Bad files/entries are skipped with a log message; a missing `data/` just gives empty categories.
- `--data=<dir>` uses another folder. `IData::reload()` re-scans and emits `core::DataReloaded`.

## What exists
| File | Contents |
|---|---|
| `data/ores.json` | 8 ores: name, `color` [r,g,b], `rarity` (spawn weight) |
| `data/ore_zones.json` | ore tiers by distance from the sun (world/asteroids, docs/WORLD.md "Ore zones"): per zone id, `min_distance` / `max_distance` (world units, [min, max), first match in file order), `weight_multiplier` {ore id: x} (missing = 1; never 0). Missing/empty file = the plain `rarity` weights |
| `data/items.json` | 8 items: name, description, color, `effect` (e.g. `{"warp_fuel": 25}`), optional `permanent` |
| `data/cargo.json` | 4 cargo hold upgrade levels: name, `stack_mult` (x every stack cap; the grid slot count is fixed): 1 / 2 / 4 / 6 (docs/INVENTORY.md) |
| `data/anomalies.json` | 5 anomaly kinds: name, `messages`, `rewards` [{ore, min, max, weight}], `detect_mult`, `weight`, optional `blueprint` (docs/ANOMALIES.md) |
| `data/blueprints.json` | blueprint id -> `name` (docs/BLUEPRINTS.md) |
| `data/recipes.json` | 8 recipes: name, `result` (item id), `ingredients` `{ore id: amount}`, optional `requires_blueprint` (docs/BLUEPRINTS.md) |

Ported from the old game's `ORE_TABLE`, `ITEM_TABLE` and `RECIPES`.

## Adding content
1. Add an entry to the JSON (or a new file in the category folder).
2. `make test` - `shipped_data_is_consistent` checks every recipe result is a real item, every ingredient a real ore,
   colors are 0..1, rarity > 0, names/descriptions non-empty. A typo fails the build.
3. Modules read what they need by id; an item's `effect` keys are interpreted by the module that owns that stat.

## Cargo data additions
- `ores.json`: optional `cargo_cap` per ore (default 20; `rock` filler 100).
- `items.json`: optional `volume` (default 1) = units of general hold.
- `cargo.json`: levels use `stack_mult`; older files fall back to `resource_mult`, then `capacity_mult`. Items may set `stack_cap` (default 10, or 10 / `volume`). Tunables `inventory.grid_columns` (8), `inventory.grid_rows` (12).
