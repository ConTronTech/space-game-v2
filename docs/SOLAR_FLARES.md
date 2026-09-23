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
`enabled` (default true), `interval_min`/`interval_max`, `warning_seconds`, `shell_speed`, `shell_thickness`, `max_range`, `damage_at_sun` (damage
at distance 0, falling off with distance the same shape as the missile blast falloff), `damage_min_range` (inside this distance of the sun, always
max damage - matches the existing instant-death-at-the-sun rule already in `ship::IShip`, `ship.sun_kills`).

## Events and service
`world::SolarFlareWarning{etaSeconds}`, `world::SolarFlareErupted{}`, `world::SolarFlareEnded{}`. `world::ISolarFlares`: `active()`,
`etaSeconds()` (-1 if none pending / already erupted this cycle), `shellRadius()` (-1 if not currently erupting) - what a HUD warning or a radar
overlay would read.
