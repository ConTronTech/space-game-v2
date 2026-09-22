# Phase 6 game loops - order and status

Referenced by docs/ROADMAP.md ("Phase 6 - Game loops (order from GAME_LOOPS.md)") but never actually written until now (2026-09-22). Each item is a
self-contained module that uses the services Phases 1-5 already built and can be deleted without breaking the rest of the game (VISION.md's
modularity rule). User decision 2026-09-22: **anomalies + scanner first** ("something to find" over "something to fight" - no NPCs exist yet, so
combat depth is parked until the user is around to steer it; see docs/QUESTIONS.md and docs/DEVLOG.md Leap 43/44's note on the fork).

1. **`world/anomalies` + scanner** - BUILT (2026-09-22, docs/ANOMALIES.md). The "what's over there?" hook: seeded points of interest scattered through the
   star system, hidden until the player crafts and uses an Anomaly Scanner (a permanent perk item, same pattern as the Ore Scanner), then shown as
   a marker on the radar/map within a detection range; flying close investigates one (one-time, saved), giving ore and/or a flavour-text discovery
   via `ui/toast` (the first real thing wired to it). See docs/ANOMALIES.md once built.
2. **`gameplay/blueprints`** - BUILT (2026-09-22, docs/BLUEPRINTS.md). Unlocks that gate crafting.
3. Ore tiers by zone (mostly data).
4. `gameplay/solar_flares`.
5. Distress beacons + black boxes.
6. Cargo pods.
7. Ship modules (upgrades).
8. Derelict turrets.
9. Solar wind + gravity assists.
10. Mining drones.
11. Environmental zones (radiation, nebula, magnetic).
12. Asteroid storms.
13. Probe network.
14. Comets.

Survival pressure (fuel decay, hull wear, O2, heat) slots in at step 2.1 as small modules reading `ShipState`, per the original roadmap note.
Combat depth (NPCs/enemies, so weapons and missiles have something to fight) is NOT on this list: it is a separate, larger design decision the
user chose to defer (docs/DEVLOG.md Leap 43/44) rather than mixed into the "things to find" loop order above.
