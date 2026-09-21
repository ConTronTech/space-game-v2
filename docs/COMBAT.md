# Weapons (`combat/weapons`)

A physical **blaster** and a **mining beam**. Design rules (docs/VISION.md): the game is hard and Newtonian, and NPCs will later follow the *same* physics and handicaps, so weapons are real objects, not cheats:
bolts **inherit the shooter's velocity**, have finite speed and lifetime, guns **heat up** (there is no ammo: a heat model limits fire), and firing **kicks the ship back**.
Inventory, ammo items and missiles/lock-on are not here (4.3 / 5 / 4.2b). Code: `package/modules/combat/weapons/` (`weapons_rules.h` is pure and unit-tested by `test_weapons.cpp`).

## Controls (default profile)
| Action | Key | |
|---|---|---|
| `fire` | left mouse button | hold to fire the selected weapon |
| `weapon_1` / `weapon_2` | 1 / 2 | blaster / mining beam |
| `weapon_next` | mouse wheel | cycle weapons |

Firing is ignored while paused, dead, warping, **docked**, and while the mouse is **not captured** (Tab = free mouse: menu clicks must not shoot). `core::IInput` does not expose the capture state, so the module uses the condition mouse look uses (`SDL_GetRelativeMouseMode()`).
Gap: `ship::IDocking::docked()` becomes true only when the docking pad approach (about 1.5 s) has finished, so you can still fire during that approach; a `busy()` on `IDocking` would close it (needs a decision, `ship/docking` is not part of this module).

## Blaster (kind `projectile`)
`fire` spawns a bolt at the muzzle (3.6 units ahead of the ship, in the ship frame) with velocity = ship velocity + aim direction (ship forward with a small seeded spread) x `speed` (600 m/s), so a bolt fired from a moving ship keeps that motion and a miss flies on past.
Each fixed step every bolt moves and its path segment is **swept** against everything it can hit (no tunnelling at any speed): the asteroids near the ship, the sun, planets and moons (`IStarSystem` spheres) and stations (`IStations`, sphere of 1.5 half-extents).
**The asteroid list is a cache** of the 256 nearest asteroids within `combat.cache_radius` (3,000 units) of the ship, refreshed at `combat.cache_hz` (4 Hz), because `IAsteroids::nearest` is O(count) and must not run per bolt per step; a cheap distance reject runs before each sphere test. Bolts far outside that radius (after the ship has flown away) no longer see asteroids.
On a hit: asteroids take the damage (`IAsteroids::damage`), everything else only gets the effect. Every hit emits `fx::SpawnParticles "spark"` at the hit point, removes the bolt, and emits `combat::ProjectileHit{targetKind, id, position, damage, shooter}` (future NPC / ship damage subscribes to it). Firing emits a `"muzzle"` burst.
The pool holds at most `combat.max_projectiles` bolts (128; struct-of-arrays, no allocation after init); a full pool drops the shot.
Drawing: one pass (`combat/weapons`, order 75, after the particles): every bolt is a 12-unit additive streak plus a small bright head square (a streak seen head-on in chase view shrinks to nothing) in **one `glDrawArrays(GL_LINES)` and one `GL_QUADS`** call, camera-relative; depth test on, depth write off; state restored.

## Mining beam (kind `beam`)
While `fire` is held: a ray from the muzzle along the ship's forward, `range` (500) units, finds the nearest asteroid it hits (sphere test against the cache), applies `beam_dps * dt` damage to it, draws a thin additive line to the hit point (or full range if nothing) and spawns a modest rate of sparks and debris at the contact (`combat.beam_particles`). It does **not** hit planets or stations.
It adds `heat_per_second`; the default beam (0.15/s against 0.3/s cooling) never overheats on its own.

## Heat, rate of fire, recoil
Each weapon has heat 0..1. A shot adds `heat_per_shot`; a beam adds `heat_per_second`; heat falls at `cooldown_rate` per second. At 1.0 the weapon **locks out** for `overheat_lockout_seconds` and comes back only when the lockout is over **and** the heat is back under 0.6.
The rate of fire is enforced by a shot timer. Default blaster: 5 shots/s x 0.1 heat against 0.25/s cooling = net +0.25/s: **overheats after about 4.4 s of continuous fire** (21 shots), locks out 2 s, recovers at heat 0.6, and so on (measured in the game).
**Recoil:** each shot sets the ship's velocity back by `recoil` m/s along the aim (`IShip::setVelocity` only), times `combat.recoil_scale` (1; 0 disables). The ship has no mass model yet, so `recoil` is "m/s per shot for a nominal ship": 0.5 m/s per shot, 40 shots = 20 m/s (seen in the speed log).

## Data: `data/weapons.json`
Read through `core::IData` (category `weapons`); built-in defaults with the same values are used when the file is missing; fields left out keep the default; values are sanitised. **The entry order is the number keys**: the first entry is weapon 1, the second weapon 2.

| Field | Meaning |
|---|---|
| `name`, `kind` | display name; `projectile` or `beam` |
| `damage` | damage per bolt |
| `speed`, `lifetime` | bolt speed relative to the shooter (m/s) and life (s) |
| `range`, `beam_dps` | beam length (units) and damage per second on asteroids |
| `rate_of_fire` | shots per second (max 60) |
| `heat_per_shot`, `heat_per_second`, `cooldown_rate`, `overheat_lockout_seconds` | the heat model |
| `recoil`, `spread` | m/s per shot; cone half angle in degrees |
| `muzzle` | `[right, up, forward]` offset in the ship frame |
| `colour` | `[r, g, b]` |

**Adding a weapon:** add an entry (kind `projectile` or `beam`) and give it a number key in `config/input/default.json` (`weapon_3`, plus one `pressed("weapon_3")` line in `weapons.cpp`); only a new *behaviour* (missiles, area damage) needs a new code path.

## Events and service (`weapons_api.h`; the HUD is not touched here)
`combat::ProjectileHit`, `combat::WeaponChanged{index, name}`, `combat::Overheated{index, name}`; `combat::ICombat`: `weaponCount()`, `selected()`, `weaponName(i)`, `heat(i)`, `overheated(i)`, `firing()`, `hitMarkerAge()` (seconds since one of our shots or the beam last hit): what a crosshair heat bar and a hit marker need.
Asteroid health, `IAsteroids::damage/alive` and `AsteroidDestroyed`: docs/WORLD.md. Audio: plays `blaster` / `impact` only if `core/audio` has a sound of that name (none is built in yet; it stays silent).

## Tunables (`config/game.json`) and laptop A/B keys
| Key | Default | |
|---|---|---|
| `combat.enabled` | true | **everything off** (no pass, no listeners) |
| `combat.max_projectiles` | 128 (Low 64, High 192, Ultra 256) | **0 = no projectiles** |
| `combat.recoil_scale` | 1.0 | 0 = no recoil |
| `combat.beam_particles` | 1.0 (Low 0.5) | beam contact particle rate multiplier |
| `combat.cache_radius` / `cache_hz` | 3000 / 4 | nearby-asteroid list |
| `asteroids.hp_scale` | 1.25 | asteroid hit points per radius^2 |

Dev flags: `--auto-fire=FRAME[,FRAMES]` holds the trigger for FRAMES engine frames from FRAME (works without a mouse and ignores the capture check, not the paused / dead / warping / docked checks); `--auto-weapon=N` starts with weapon N selected (1 = blaster, 2 = beam).
At debug log level the module prints heat, live bolts, ship speed and CPU per step once a second while firing, every hit, and `fire ignored: docked` for scripted fire.

## Cost
Pure geometry, measured on the dev machine (`test_weapons`): 100 live bolts against 256 nearby asteroids = **0.05 ms per fixed step** (about 3 ms per second); in the game a step with a handful of bolts costs about 0.01 ms.
Drawing is one line call and one quad call, no texture, no full-screen layer.

## HUD
`ui/ship_hud` shows the selected weapon's heat beside the crosshair, a hit marker (`hitMarkerAge()`, red on `AsteroidDestroyed`), a bottom-left weapon block with heat bars, and "BLASTER OVERHEATED" / "WEAPON: ..." banners: see docs/HUD.md. It only reads `ICombat` and the events; nothing changes in this module.
