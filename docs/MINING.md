# Mining (`gameplay/mining`)

Weapons destroy asteroids (docs/COMBAT.md, docs/WORLD.md); mining turns a destroyed asteroid into **ore chunks** the player has to fly to and scoop into the cargo hold (docs/INVENTORY.md). Code: `package/modules/gameplay/mining/` (`mining_rules.h` is pure and unit-tested by `test_mining.cpp`).

## From rock to ore
On `world::AsteroidDestroyed{id, position, radius, ore}` the module breaks the rock into chunks:
* **Yield:** `mining.yield_scale * radius^2` ore units, at least 1 (scale 0.5: a radius-9 rock holds 41 units, radius 3 holds 5, radius 60 about 1,800).
* **Ore** is the asteroid's own ore id (`world::IAsteroids::ore(i)`, from `data/ores.json` by rarity); an unknown or "rock" ore yields `rock`: worthless filler that still takes cargo space.
* **Chunks:** about `1 + sqrt(total)` (times `mining.chunk_count_scale`), between 1 and min(total, 16); the total is split evenly, so nothing is lost or invented.
* Each chunk starts at the asteroid and flies outward at 2-10 m/s in a seeded random direction (the asteroid itself is static: no extra velocity), then **drifts forever, Newtonian, no drag**.

## Scooping (hard on purpose)
A chunk is collected into the hold when the ship is within `mining.scoop_radius` (12 units) **and** slower than `mining.scoop_speed` (40 m/s) **relative to the chunk**: a fast fly-through collects nothing, you must slow down and match.
If the hold is nearly full the chunk is scooped **partially** (`inventory.add` accepts what fits, the inventory logs and emits `CargoFull`) and the remainder stays in the chunk. A refused chunk is not retried until the ship leaves the scoop radius and comes back (no message spam).
Each pickup emits `gameplay::OreMined{ore, amount}` (`mining_api.h`; the amount that fitted), a small `fx::SpawnParticles "spark"` in the ore colour, and plays a `pickup` sound only if `core/audio` has one (none is built in). It needs `gameplay/inventory`; without it chunks still appear but cannot be collected.
Chunks disappear after `mining.chunk_lifetime` (300 s) or when they are farther than `mining.chunk_range` (3,000 units) from the ship.

## Drawing and cost
The pool holds at most `mining.max_chunks` chunks (96; Low 48, High 128, Ultra 192; the **oldest** is dropped when full). One render pass (`mining/chunks`, order 66: after the asteroids and stations, before the particles): all chunks are camera-facing quads in the ore colour (brightened) in **one `glDrawArrays(GL_QUADS)`**, at least ~3 pixels wide at any distance, no texture, no blending, depth test on, depth write off, state restored.
Cost: 96 chunks = 0.0006 ms per fixed step for advancing and the scoop test (unit test); 0.002-0.003 ms per step in the game; drawing is one call.

## Tunables (`config/game.json`)
| Key | Default | |
|---|---|---|
| `mining.enabled` | true | **false = nothing** (no chunks, no pass; rocks still break) |
| `mining.yield_scale` | 0.5 | ore units = scale x radius^2 |
| `mining.chunk_count_scale` | 1.0 | more or fewer chunks |
| `mining.chunk_lifetime` | 300 | seconds |
| `mining.chunk_range` | 3000 | units from the ship |
| `mining.scoop_radius` | 12 | units |
| `mining.scoop_speed` | 40 | m/s relative |
| `mining.max_chunks` | 96 (preset 48 / 96 / 128 / 192) | 0 = no chunks |

At debug log level: the chunk count and CPU per step every 5 s. Every break-up and pickup is logged at info level.

## Known gaps
* **Ore chunks are not saved** (they are lost on load), and **destroyed asteroids are not saved either**: the field regenerates on load, so a destroyed rock comes back. A later persistence step (saving the destroyed ids) fixes both.
* No HUD line for cargo or pickups yet; no sound (no `pickup` sound exists); nothing consumes ore yet (crafting is 5.5).

## Capsule collection (4.3b)
- `mining.mode` = `capsule` (default) | `instant` | `chunks`.
- capsule: a destroyed asteroid leaves ONE flashing capsule holding all its ore (ore-colour diamond, white core). Life `mining.capsule_seconds` (30), flash speeds up in the last 5 s. Within `mining.magnet_radius` (120) it is pulled toward the ship; within `mining.scoop_radius` (20) it is collected at any speed (`mining.scoop_speed` 0 = no limit). If that ore's hold is full it stays, flashes RED until it expires, and `CargoFull` is emitted once; it is retried when room appears. A partial fit leaves the remainder in the capsule.
- instant: ore goes straight into the hold; a remainder that does not fit becomes a (red) capsule.
- A rock destroyed beyond `mining.chunk_range` goes straight to the hold in any mode.
- chunks: the old scattered chunks (`chunk_*` tunables). `mining.max_chunks` sizes both pools; the quality rows still apply.
