# Solar flares (`world/solar_flares`, Phase 6.4)

The next unbuilt Phase 6 game loop item (`docs/GAME_LOOPS.md` item 4). A survival-pressure hazard that doesn't need NPCs: the sun periodically
erupts, sending an expanding damage front outward through the system. You get a warning, and the danger is purely about position and timing, not
combat.

## Module
`package/modules/world/solar_flares/`: `solar_flares.cpp` (module, optional `world::ISolarFlares`, no save needed - just re-roll the next flare
time on load, deterministic drift is fine), `solar_flares_api.h` (service + events), `flare_rules.h` (pure: timer roll, shell radius over time,
damage-at-distance falloff, warning countdown; unit-tested in `package/tests/test_solar_flares.cpp`).
- Requires `world/star_system` (the sun's position/radius) and `core/physics_world`/`ship/ship_core` (to damage the ship). Optional: `ui/toast` or
  the HUD (a warning line), `ship/docking` (docked ships are immune - flares are why stations exist as safe harbors).
- Delete the folder and the game runs as before.

## Behavior
- **Timer**: next flare at a seeded random interval, `solar_flares.interval_min` to `solar_flares.interval_max` seconds (suggest 90-240s), rolled
  fresh after each eruption (not a fixed schedule - keeps it a real "watch for it" hazard, not a metronome).
- **Warning**: `solar_flares.warning_seconds` (suggest 15s) before eruption, emit `world::SolarFlareWarning{etaSeconds}` every fixed step so the
  HUD/toast can show a countdown ("SOLAR FLARE IN 12s"). Simulation time only (frozen while paused, like everything else fixed-step).
- **Eruption**: emits `world::SolarFlareErupted{}` once, then an expanding spherical shell centered on the sun grows outward at
  `solar_flares.shell_speed` (units/s) until it passes `solar_flares.max_range` (suggest 3x the outermost planet orbit) and the flare ends.
- **Damage**: while the ship is within `solar_flares.shell_thickness` units of the shell's current radius (i.e. the front is passing through it
  right now, not "anywhere inside a growing sphere" - a one-time pass, not a standing hazard), it takes damage through the normal shield-first
  `IShip::applyDamage` model, scaled by how close to the SUN it was when hit (closer = worse - use the same kind of distance-falloff idea as the
  missile blast radius in `docs/COMBAT.md`, not a flat number).
- **Docked ships are immune** (`ship::IDocking::docked()` true skips damage entirely - this is the mechanical reason to make it to a station before
  a flare arrives, without needing to invent a new "shielded zone" concept).
- **No damage to stations, planets or asteroids** - this is a ship-survival hazard only, keep scope tight for a first version.

## Data / tunables (`config/game.json`, key `solar_flares.*`)
`enabled` (default true), `interval_min`/`interval_max`, `warning_seconds`, `shell_speed`, `shell_thickness`, `max_range` (how far the front
physically travels before the flare ends), `damage_at_sun` (damage at distance 0, falling off with distance the same shape as the missile blast
falloff), `damage_min_range` (inside this distance of the sun, always max damage - matches the existing instant-death-at-the-sun rule already in
`ship::IShip`, `ship.sun_kills`), `damage_falloff_range` (damage reaches 0 here - independent of `max_range`, which is far past the outer planets
by design: without its own, much shorter falloff, damage would barely drop by the time the front reaches most of the system).

## Events and service
`world::SolarFlareWarning{etaSeconds}`, `world::SolarFlareErupted{}`, `world::SolarFlareEnded{}`. `world::ISolarFlares`: `active()`,
`etaSeconds()` (-1 if none pending / already erupted this cycle), `shellRadius()` (-1 if not currently erupting) - what a HUD warning or a radar
overlay would read.

## As built (2026-09-22)
- Folder is `world/solar_flares` (the GAME_LOOPS.md line said `gameplay/`; it depends on the star system and hurts only the ship, so it sits with
  the other world hazards). Required: `world/star_system`. Optional: `ship/ship_core`, `ship/docking`, `ui/toast`.
- Defaults: `interval_min` 90, `interval_max` 240, `warning_seconds` 15, `shell_speed` 4000, `shell_thickness` 1500, `max_range` 0 (= auto,
  3x the outermost planet orbit), `damage_at_sun` 120, `damage_min_range` 5000, `damage_falloff_range` 30000 (units from the sun's centre).
  Distances are from the sun's centre; the front starts at the sun's surface. **Fixed post-build (2026-09-22, coordinator review):** the first
  draft used `max_range` as the damage falloff distance too, so with the auto `max_range` (~3x the outer orbit) damage barely dropped by the
  time the front reached most planets - `damage_falloff_range` is now a separate, much shorter tunable so most of the system is actually safe
  from flare damage and only the region close to the sun is genuinely dangerous.
- The next countdown starts when a flare ENDS (the front passes `max_range`), so two fronts never overlap.
- One hit per flare: the hit test is swept (the front's radius at the previous step to this step, +/- half the thickness), so a fast front or
  a long step never skips the ship. If the front passes while docked, that flare is spent for the ship (no second try after undocking).
- Toasts: one amber "SOLAR FLARE IN Ns - dock at a station" when the warning starts, one red "SOLAR FLARE ERUPTED". A live HUD countdown (read
  `ISolarFlares::etaSeconds()` or `SolarFlareWarning`) is a follow-up. Damage source string: `"solar_flare"`.
- Test flag `--solar-flare-now`: the first eruption comes `warning_seconds + 1` s after boot.
