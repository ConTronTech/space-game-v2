# Cargo pods (`world/cargo_pods`, Phase 6.6)

`docs/GAME_LOOPS.md` item 6. Ambient, low-effort loot: unlike a distress beacon (one at a time, urgent, expires) or an anomaly (scanner-gated,
one-time, saved), cargo pods are a small **pool** of several drifting containers scattered through the system at once, always visible (no perk),
each holding a modest mixed reward (ore and/or a crafted item), continuously respawning so there's always something small to find while exploring
or flying a route - background income, not an event.

## Module
`package/modules/world/cargo_pods/`: `cargo_pods.cpp` (module, `world::ICargoPods`), `cargo_pods_api.h` (service + events), `pod_rules.h` (pure:
seeded pool of pod slots, each independently timed spawn/respawn, magnet+scoop pickup test reusing the SHAPE of `gameplay/mining`'s capsule
mechanic (`mining.magnet_radius`/`mining.scoop_radius` - read `docs/MINING.md` "Capsule collection" first and mirror that pattern rather than
inventing a new pickup feel), reward roll; unit-tested in `package/tests/test_cargo_pods.cpp`).
- Requires `world/star_system`. Optional: `gameplay/inventory`, `ui/toast` (or no toast at all - see below), `ship/ship_core`,
  `world/asteroids`/`world/stations` (placement, same trimmed zone-pick idea as `world/distress_beacons`' `beacon_rules.h` - reuse/mirror that
  code, don't re-derive it from anomalies again).
- Delete the folder and the game runs as before.

## Behavior
- **Pool size**: `cargo_pods.pool_size` (suggest 5-8) independent pod slots. Each slot: empty (waiting) -> spawned (a pod sits at a seeded
  position) -> collected or expired -> a fresh seeded wait before that SLOT spawns again. Slots are independent (not synced), so pods appear
  and disappear at different times, keeping the field feeling ambient rather than an event.
- **Placement**: same zone logic as distress beacons (deep space / near body / belt), no "reachable within N seconds" constraint needed this
  time (pods aren't urgent - there's no rush, and with several at once some will always be nearby eventually).
- **Visible always** (no perk, no detect-range gating needed the way beacons have one - these are meant to be stumbled into, not signalled
  toward; a very large or unlimited detect range is fine, or skip a detect concept and just let the radar/world show them directly like
  asteroids do).
- **Pickup**: magnet-then-scoop like a mining capsule (`mining.magnet_radius`/`mining.scoop_radius` tunables can be reused directly by reading
  the SAME config keys, since it's the same feel - simpler than inventing separate `cargo_pods.*` pickup-radius tunables; note this choice or
  the alternative you pick).
- **Reward**: modest and mixed - a small amount of one seeded ore id AND/OR a small chance of a crafted item, noticeably smaller than a distress
  beacon's reward (this is ambient background income, beacons are the "notice and react" reward). A weighted table (like anomalies' `rollAnomaly`)
  is fine here since pods repeat often - reuse that pattern if convenient.
- **No per-pod toast spam** - with several pods spawning/expiring independently and often, a toast on every single one would be noisy. Either no
  toast at all (log only), or a much rarer/quieter one - your call, note which.
- **Expiry**: pods can optionally expire after a while if unclaimed and respawn elsewhere (keeps the field fresh) - or just persist until
  collected, simpler. Note which you picked; either is fine for a first version.

## Data / tunables (`config/game.json`, key `cargo_pods.*`)
`enabled` (default true), `pool_size`, `respawn_min`/`respawn_max` (seconds a slot waits after being emptied before it spawns again),
`ore_reward_min`/`_max`, `item_chance` (0..1, chance of a bonus crafted item on top of the ore).

## Events and service
`world::CargoPodSpawned{slot, x, y, z}`, `world::CargoPodCollected{slot, oreGiven, amount}`. `world::ICargoPods`: `count()` (active pods right
now), `position(i)`, `alive(i)` - enough for a future radar/world-marker pass, matching the shape of `world::IAsteroids`'s query API.

## As built (2026-09-22)
- Required: `world/star_system`. Optional: `core/data_registry` (ore rarities, item names), `core/render_engine` (the pod markers),
  `core/audio`, `fx/particles`, `gameplay/inventory`, `ui/toast`, `ship/ship_core`, `world/asteroids` (belt sites), `world/stations`
  (near-body clearance).
- Defaults: `enabled` true, `pool_size` 6 (max 32), `respawn_min` 60, `respawn_max` 240, `lifetime_seconds` 900 (0 = until collected),
  `ore_reward_min` 2, `ore_reward_max` 6, `item_chance` 0.15, `item_ids` `"repair_kit,missile_pack"`, `drift_speed` 2 m/s.
- Slots: each has its own seed (`podSlotSeed(seed, slot)`) and cycle counter. The boot wait is uniform in [0, `respawn_min`) so the field
  fills (staggered) within the first minute; later waits are [`respawn_min`, `respawn_max`].
- Expiry: **expire and relocate** - an unclaimed pod drifts off after `lifetime_seconds` and its slot re-rolls a fresh site/reward after a
  new wait. `lifetime_seconds` 0 keeps pods until collected.
- Placement: `pod_rules.h` holds a trimmed **copy** of `world/distress_beacons`' zone pick (same zones, margins, ring gaps; not an include
  - that would make deleting `world/distress_beacons` break this module). One candidate per zone; a seeded zone is kept unless it is within
  2x the magnet radius of the ship (then the next zone; all too close: the farthest). No reachability constraint. Near-body pods move with
  their body. Every pod drifts slowly (seeded direction, 0.25-1x `drift_speed`).
- Pickup: reads the **same** `mining.magnet_radius` / `magnet_speed` / `scoop_radius` / `scoop_speed` keys (and defaults) as the mining
  capsules, and a copy of their magnet formula. **Coupling:** change those keys and both change. Hold full: the remainder stays in the pod,
  which stops and flashes red, and is retried once there is room (like a capsule).
- Reward: rolled at spawn from the site seed: a rarity-weighted ore (data/ores.json `rarity`, rock excluded) x 2-6 units, plus with
  `item_chance` one uniform pick from `item_ids` (unknown ids skipped with a warning).
- Feedback: ore emits `gameplay::OreMined` (the ship HUD's "+N ORE" banner), plus sparks and the "pickup" sound. The **only** toast is a
  short cyan "Cargo pod: REPAIR KIT recovered" when a bonus item is picked up; spawns and expiries are log-only.
- Drawing: a camera-facing amber square with a pulsing white diamond core (the mining capsule billboard, recoloured), never under a few
  pixels, pass `world/cargo_pods` at 67; clipped beyond the camera far plane. Red while the hold refuses it.
- Service: `count()` is the SLOT count (stable indices, like `IAsteroids::count()`), `alive(i)` / `position(i)` per slot, plus
  `activeCount()` for "pods right now". Events: `CargoPodSpawned{slot, x, y, z}`, `CargoPodCollected{slot, oreGiven, amount, item}` (once
  per pickup; a partial pickup emits again when the rest is taken).
- No save: a reload re-rolls every slot from the seed.
- Test flag `--cargo-pods-now`: every slot spawns within the first ~2.5 s.
- Follow-ups: a radar/HUD marker (read `ICargoPods`); maybe hoist the zone pick (now copied twice) into `world/star_system`.
