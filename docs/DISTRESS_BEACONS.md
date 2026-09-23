# Distress beacons + black boxes (`world/distress_beacons`, Phase 6.5)

`docs/GAME_LOOPS.md` item 5. A recurring "someone needs help / something is out there" hook, distinct from `world/anomalies` (item 1): anomalies
are a FIXED, one-time-per-seed set of sites gated behind the Anomaly Scanner perk. Distress beacons are the opposite shape - a RECURRING, timed
event (no scanner needed, a beacon is loud on purpose) that expires if you don't reach it in time, so it's about noticing and reacting, not
scanning and exploring.

## Module
`package/modules/world/distress_beacons/`: `distress_beacons.cpp` (module, `world::IDistressBeacons`, no save needed - like solar flares, the
next beacon timer just re-rolls from the seed on load; an active beacon simply isn't there anymore after a reload, which is fine, it's a timed
event not a permanent site), `distress_beacons_api.h` (service + events), `beacon_rules.h` (pure: seeded spawn timer and position, decay/expiry
countdown, investigation range check; unit-tested in `package/tests/test_distress_beacons.cpp`).
- Requires `world/star_system`. Optional: `gameplay/inventory` (the black box + any ore reward), `ui/toast`, `ship/ship_core` (position).
- Delete the folder and the game runs as before.

## Behavior
- **Spawn timer**: like solar flares' `FlareParams`-style interval roll - a seeded random gap (`distress_beacons.interval_min` to
  `..._max` seconds, suggest 60-180s) between one beacon expiring/being found and the next one spawning. At most ONE active beacon at a time
  (keep scope tight for a first version - a beacon swarm is a later idea, not this pass).
- **Position**: reuse the SAME zone logic as `world::anomalies`' `generateAnomalies` (deep space between two orbits / near a body / in a belt) -
  don't reinvent placement, call into (or copy the pure logic pattern of) `world/anomalies/anomaly_rules.h`'s zone picker if it's reusable as a
  pure function, otherwise duplicate the minimal seeded-zone-pick logic rather than a full rewrite.
- **Always visible on the radar once spawned** (no perk needed - a distress signal is a broadcast, unlike a buried anomaly), within
  `distress_beacons.detect_range` (suggest a large range - it's a signal, not a buried site).
- **Expiry**: `distress_beacons.lifetime_seconds` (suggest 90-150s) after spawning, an un-investigated beacon disappears (a toast: "The distress
  signal has gone quiet." - someone else got there first, or it burned out; no mechanical penalty, just a missed reward). A live countdown
  (`etaExpirySeconds()` on the service) lets a HUD worker show urgency later - not required for this pass.
- **Investigation**: fly within `distress_beacons.investigate_range` (suggest 75, matching anomalies) of an active beacon to claim it: gives a
  **black box** item (add `"black_box"` to `data/items.json` if it's not already defined - flavor: a data recorder, no mechanical effect yet,
  just a collectible/flavor item, matching how `docs/BLUEPRINTS.md`/anomalies introduced flavor rewards) plus a small ore reward (reuse the same
  weighted-reward-table idea `anomaly_rules.h`'s `rollAnomaly` uses, or keep it simpler - a fixed small ore amount is fine for a first version,
  note the choice). One-time per beacon (it's gone once claimed, same as investigating an anomaly site).
- **Toast on spawn**: "DISTRESS SIGNAL DETECTED" (urgent-ish, but not as alarming as a solar flare warning - this is an opportunity, not a
  threat).

## Data / tunables (`config/game.json`, key `distress_beacons.*`)
`enabled` (default true), `interval_min`/`interval_max`, `lifetime_seconds`, `detect_range`, `investigate_range`, `ore_reward_id` (which ore the
black box run gives, suggest a mid-tier one like `"cobalt"` so it's a real early-game incentive), `ore_reward_amount`.

## Events and service
`world::DistressBeaconSpawned{siteSeed, x, y, z}`, `world::DistressBeaconInvestigated{oreGiven, amount}`, `world::DistressBeaconExpired{}`.
`world::IDistressBeacons`: `active()`, `position()` (world-space, only valid while active), `etaExpirySeconds()` (-1 if none active) - what a
future radar marker or HUD countdown would read.
