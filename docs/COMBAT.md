# Weapons (`combat/weapons`)

A physical **blaster**, a **mining beam** and guided **missiles** with lock-on. Design rules (docs/VISION.md): the game is hard and Newtonian, and NPCs will later follow the *same* physics and handicaps, so weapons are real objects, not cheats:
bolts **inherit the shooter's velocity**, have finite speed and lifetime, guns **heat up** (the blaster and beam have no ammo: a heat model limits fire), firing **kicks the ship back**, and missiles are real objects with finite fuel, thrust and turn rate (ammo from crafted Missile Packs).
Code: `package/modules/combat/weapons/` (`weapons_rules.h` and `missile_rules.h` are pure and unit-tested by `test_weapons.cpp` / `test_missiles.cpp`).

## Controls (default profile)
| Action | Key | |
|---|---|---|
| `fire` | left mouse button | hold to fire the selected weapon |
| `weapon_1` / `weapon_2` / `weapon_3` | 1 / 2 / 3 | blaster / mining beam / missiles |
| `lock_target` | T | lock the best rock in the cone; again = next candidate; on the only candidate = clear |
| `lock_clear` | Y | drop the lock |
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

## Missiles (kind `missile`, weapon 3)
**Ammo, not heat:** a rack of `combat.max_missiles` (12) held by this module (`combat.start_missiles`, 0; saved). A Missile Pack (+3, gameplay/crafting) goes through `combat::IAmmo::addMissiles(n)`: a full rack refuses ("missile rack full") and the pack is not consumed; a nearly full one takes what fits.
`fire` launches one every 1.5 s (`rate_of_fire` 0.667, the shot timer); an empty rack refuses with the log line `NO MISSILES` and `ICombat::noAmmo()` (the HUD shows "NO MISSILES"); a full pool (`combat.max_missiles_alive`, 8) refuses too.
**Launch:** at the muzzle, velocity = ship velocity + `launch_kick` (40 m/s) along the nose. It carries the locked target (only a finished lock; acquiring does not count) by asteroid id.
**Flight (per fixed step, `stepMissile`):** while `fuel_seconds` (8 s) last it thrusts `thrust` (60 m/s^2) along its nose (burnout speed ~520 m/s relative to the launch); after that it coasts with no control. With a target it steers by **proportional navigation**:
`a = N * Vc * (Omega x LOS)` (Omega = line-of-sight rotation rate from the relative position/velocity, Vc = closing speed, kept >= 20 m/s, `nav_gain` N = 4); the rest of the thrust goes along the line of sight, and the nose turns towards that at most `turn_rate` (90 deg/s). A command above the thrust is clamped. Without a target (or after it died) it flies straight along its launch heading.
The guidance is written for moving targets (unit-tested against a crossing target at ~63 m/s); asteroids are static, so in the game it is steering to a point.
**Detonation:** only after `arm_distance` (30 units) travelled. Then each step's segment is swept (like bolts) against the nearby asteroids, sun/planets/moons and stations -> impact; against the target sphere grown by `fuse_radius` (8) -> proximity fuse; `lifetime` (14 s) over -> it explodes where it is (a missile that ran out of fuel coasts until then).
**Explosion:** every rock whose **surface** is within `blast_radius` (60) of the explosion takes `damage * (1 - d / blast_radius)` (`damage` 400 at the centre): a radius-9 rock (~100 HP) dies to one missile, big rocks need several. One `combat::ProjectileHit{"asteroid", ...}` per damaged rock, one `combat::MissileExploded{position, radius, reason, shooter}`, and an fx `explosion` (new preset: 6-9 big soft additive particles) + `debris` + `spark` burst.
**No damage to the player's own ship** (there is no friendly-fire / ship-damage model yet); planets, moons, the sun and stations only stop the missile.
Pool: struct of arrays (`MissilePool`, double position/velocity), no allocation after init. Drawn in the same pass and the same two draw calls as bolts: a 25-unit streak (8 once the fuel is out) along the nose and a bigger hot head square.

## Lock-on (T / Y)
Candidates: alive asteroids of the nearby list (see the cache below) within `combat.lock_range` (2500) and inside the `combat.lock_cone_deg` (12 deg) cone around the nose, ranked by angle (half a degree counts as equal), then distance. Stations are not lockable.
State machine (pure, `updateLock`): **idle -> acquiring** (T) **-> locked** after `combat.lock_time` (1.0 s) inside the cone **-> lost** when the target is destroyed, out of range, or leaves the cone (the narrow cone while acquiring, the wider `combat.lock_keep_cone_deg` 30 deg once locked); lost shows for 1 s, then idle.
T with a target cycles to the next candidate (acquiring again); T when the current target is the only candidate clears; Y clears.
**Marker:** a diamond around the target (at least 1.6 x its radius, and never smaller than ~2.5 % of its distance), camera-relative lines in the weapons pass' line batch: amber and blinking while acquiring, solid red with four inner ticks when locked. No extra draw call, no full-screen layer.

## Heat, rate of fire, recoil
Each weapon has heat 0..1. A shot adds `heat_per_shot`; a beam adds `heat_per_second`; heat falls at `cooldown_rate` per second. At 1.0 the weapon **locks out** for `overheat_lockout_seconds` and comes back only when the lockout is over **and** the heat is back under 0.6.
The rate of fire is enforced by a shot timer. Default blaster: 5 shots/s x 0.1 heat against 0.25/s cooling = net +0.25/s: **overheats after about 4.4 s of continuous fire** (21 shots), locks out 2 s, recovers at heat 0.6, and so on (measured in the game).
**Recoil:** each shot sets the ship's velocity back by `recoil` m/s along the aim (`IShip::setVelocity` only), times `combat.recoil_scale` (1; 0 disables). The ship has no mass model yet, so `recoil` is "m/s per shot for a nominal ship": 0.5 m/s per shot, 40 shots = 20 m/s (seen in the speed log).

## Data: `data/weapons.json`
Read through `core::IData` (category `weapons`); built-in defaults with the same values are used when the file is missing; fields left out keep the default; values are sanitised. **The entry order is the number keys**: the first entry is weapon 1, the second weapon 2, the third (missiles) weapon 3.

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

**Adding a weapon:** add an entry (kind `projectile`, `beam` or `missile`) and give it a number key in `config/input/default.json` (`weapon_4`, plus one `pressed("weapon_4")` line in `weapons.cpp`); only a new *behaviour* (missiles, area damage) needs a new code path.

## Events and service (`weapons_api.h`; the HUD is not touched here)
`combat::ProjectileHit`, `combat::WeaponChanged{index, name}`, `combat::Overheated{index, name}`, `combat::MissileExploded{position, radius, reason, shooter}`; `combat::ICombat`: `weaponCount()`, `selected()`, `weaponName(i)`, `heat(i)`, `overheated(i)`, `firing()`, `hitMarkerAge()` (seconds since one of our shots or the beam last hit): what a crosshair heat bar and a hit marker need.
Missile additions (non-pure, for the HUD worker): `ammo(i)` (missiles left for a missile weapon, -1 = unlimited), `noAmmo()` ("NO MISSILES"), `missileCount()` (in flight), `lockState()` (`LockStateId` idle/acquiring/locked/lost), `lockTargetName()` ("asteroid 4092 (gold)"), `lockTargetDistance()`, `lockTargetAngle()` (deg off the nose), `lockProgress()` (0..1).
`combat::IAmmo`: `addMissiles(n)` -> accepted, `missiles()`, `maxMissiles()`.
**Save** (`ISaveable` id `combat/weapons`): `{"missiles": N, "selected": index}`; missing keys keep the current values, values are clamped.
Asteroid health, `IAsteroids::damage/alive` and `AsteroidDestroyed`: docs/WORLD.md. Audio: plays `blaster` / `impact` only if `core/audio` has a sound of that name (none is built in yet; it stays silent).

## Tunables (`config/game.json`) and laptop A/B keys
| Key | Default | |
|---|---|---|
| `combat.enabled` | true | **everything off** (no pass, no listeners) |
| `combat.max_projectiles` | 128 (Low 64, High 192, Ultra 256) | **0 = no projectiles** |
| `combat.recoil_scale` | 1.0 | 0 = no recoil |
| `combat.beam_particles` | 1.0 (Low 0.5) | beam contact particle rate multiplier |
| `combat.cache_radius` / `cache_hz` | 3000 / 4 | nearby-asteroid list |
| `combat.max_missiles_alive` | 8 (Low 4, Ultra 12) | **0 = missiles cannot be launched** |
| `combat.max_missiles` / `start_missiles` | 12 / 0 | rack size / missiles at a new game |
| `combat.lock_range` / `lock_cone_deg` / `lock_keep_cone_deg` / `lock_time` | 2500 / 12 / 30 / 1.0 | lock-on |
| `asteroids.hp_scale` | 1.25 | asteroid hit points per radius^2 |

Dev flags: `--auto-fire=FRAME[,FRAMES]` holds the trigger for FRAMES engine frames from FRAME (works without a mouse and ignores the capture check, not the paused / dead / warping / docked checks); `--auto-weapon=N` starts with weapon N selected (1 = blaster, 2 = beam, 3 = missiles); `--give-missiles=N` fills the rack; `--auto-lock=FRAME` presses T at that frame.
Scripted missile scenario (a saved game with a rock ahead; load it with `--paused=load --ui-click=400,297,20` at 800x600):
`--give-missiles=3 --auto-weapon=3 --auto-lock=80 --auto-fire=220,1`. At debug level every missile logs position / speed / fuel / life / target distance twice a second, acquiring logs distance / angle / timer, and a T press that finds nothing logs the big rocks nearby and the nearest rock (where to point a test ship).
At debug log level the module prints heat, live bolts, ship speed and CPU per step once a second while firing, every hit, and `fire ignored: docked` for scripted fire.

## Cost
Pure geometry, measured on the dev machine (`test_weapons`): 100 live bolts against 256 nearby asteroids = **0.05 ms per fixed step** (about 3 ms per second); in the game a step with a handful of bolts costs about 0.01 ms.
Drawing is one line call and one quad call, no texture, no full-screen layer.
Missiles (measured in the game, desktop): a step with 8-9 missiles in flight costs **0.024-0.027 ms** (0.012 ms with none); PN guidance alone for 8 missiles is 0.0006 ms (`test_missiles`). An explosion runs one `IAsteroids::nearest(32)` (O(count), once per explosion).

### Verified (4.2b, saved-game scenarios)
- Locked a radius-9.6 gold rock 858 units ahead in 1.0 s (angle 1.0 -> 1.9 deg while the ship drifted at 20 m/s), missile launched, speed 57 -> 292 m/s over 4 s while fuel went 8.0 -> 4.0, proximity fuse at 8 units: 347 damage, `asteroid 4092 destroyed`, lock lost "target destroyed", then idle.
- Unguided: flew straight along the launch heading, burned out at 534 m/s, coasted, exploded "fuel and lifetime out" 14 s after launch.
- `NO MISSILES` refusal with an empty rack; launch refused with `combat.max_missiles_alive` 0; save `{"missiles":4,"selected":2}` and load of 5 missiles / weapon 3.
- Game menu: Missile Pack at 11/12 -> +1 (12/12), a second one refused "missile rack full" and kept; Ore Scanner installed once, the second refused and kept.

## HUD
`ui/ship_hud` shows the selected weapon's heat beside the crosshair, a hit marker (`hitMarkerAge()`, red on `AsteroidDestroyed`), a bottom-left weapon block with heat bars, and "BLASTER OVERHEATED" / "WEAPON: ..." banners: see docs/HUD.md. It only reads `ICombat` and the events; nothing changes in this module.
