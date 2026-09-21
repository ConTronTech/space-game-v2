# Ship (`ship/ship_core`)

`ship/ship_core` is the player's ship: 6-axis Newtonian flight (formerly the `gameplay/flight` demo), HP, an optional disposable
shield, warp-fuel storage, collision damage and death. It provides `ship::IShip` (`package/modules/ship/ship_core/ship_api.h`) and
`core::ITransformSource` (the camera follows it). It draws no HUD: `ui/ship_hud` reads `IShip` and the events.
The warp drive itself, the respawn timer/overlay and the sun's kill zone are other modules; this one only stores and reacts.

## Using it from another module
```cpp
auto* ship = eng.services.get<ship::IShip>();          // null if ship/ship_core is off; list it in optionalDependencies()
const ship::ShipStatus& s = ship->status();            // hp, maxHp, shield, warpFuel, alive, speed ... (a live reference)
ship->applyDamage(25.0f, "laser");                     // shield first, then hull
if (ship->consumeWarpFuel(dt * 3.33f)) { ... }         // false and nothing taken when there is not enough
eng.events.subscribe<ship::Died>([](const ship::Died& d) { /* d.cause */ });
```
Events (`engine.events`): `DamageTaken{amount (reached the hull), absorbedByShield, source}` (also when the shield took it all),
`ShieldBroken`, `Died{cause}`, `Respawned`, `FuelEmpty` (fuel just went from >0 to 0). A dead ship ignores `applyDamage`/`heal`.

| Call | Effect |
|---|---|
| `applyDamage(a, src)` | shield absorbs first (installed + enabled + > 0). A shield that runs out is removed (`shieldInstalled=false`, `ShieldBroken`). hp 0 -> `alive=false`, `Died{src}` |
| `heal(hp)` | clamps to `maxHp`; ignored when dead |
| `addWarpFuel(a)` / `consumeWarpFuel(a)` | clamp to `maxWarpFuel`; consume returns false if not enough; `FuelEmpty` on reaching 0 |
| `addMaxHp(a)` | permanent, capped at `ship.max_hp_cap` (200); current hp is not topped up (old game) |
| `installShield(true/false)` | generator: fills the shield to `maxShield`; `false` removes it |
| `setVelocity(v)` | e.g. warp drive, docking |
| `setHeld(bool)` / `setPose(pos, fwd, up)` | **docking only** (non-pure, no-op defaults): while held the ship ignores rotation/thrust input and its own integration, and ignores collisions with `station` bodies; the holder places it every fixed step with `setPose` (orthonormalised, angular rates zeroed, physics body synced, the camera keeps interpolating from the previous step's pose) and sets its velocity with `setVelocity`. `setHeld(false)` gives control back. See docs/STATIONS.md |
| `kill(cause)` | instant death, `Died{cause}` |
| `respawn()` | instant reset at the spawn point (origin) facing -Z: velocity 0, full hull and fuel (shield not restored, like the old game), `Respawned` |

`ShipStatus::warping` is not driven here; the warp-drive module owns that concept.

## Behavior
- **Flight:** unchanged from the demo (`flight.*` tunables, eased controls, drift/brake/max_speed). While dead, controls are ignored and the ship coasts; the engine hum is silent.
- **Regen:** passive hull regen while alive, in the fixed update, so it is frozen while paused. The shield never regenerates.
- **Collisions:** on `core::Collided` the ship bounces (`flight.bounce`) and takes `max(min, closingSpeed/refSpeed * base)` damage, where base depends on the
  other body's kind: `asteroid` radius <5 -> 10, <30 -> 20, else 35; `planet`/`moon` 50; anything else 10. Kind `sun` is instant death (`Died{"sun"}`).
  Every hit does at least 1 HP (scratch). Plays the `impact` sound.
- The pure rules (damage through shield, regen, fuel, formula) are in `ship_rules.h` (no SDL/GL) and tested by `package/tests/test_ship_rules.cpp`.

## Tunables (`config/game.json`)
`flight.*`: thrust, turn_rate, turn_tau, engine_tau, drift, assist_strength, brake, max_speed, hull_radius, bounce (as before).
`ship.*`: `max_hp` 100, `max_shield` 200, `max_warp_fuel` 100, `hp_regen` 0.2 (HP/s), `max_hp_cap` 200, `damage_ref_speed` 200 (m/s),
`damage_asteroid_small` 10, `damage_asteroid_med` 20, `damage_asteroid_big` 35, `damage_planet` 50, `damage_other` 10, `damage_min` 1, **`damage_min_speed` 3, `contact_cooldown` 0.6**.

## Saved fields
Save id is still **`gameplay/flight`** (kept from the demo so existing saves load): `pos`, `vel`, `fwd`, `up`, and the stats
`hp`, `maxHp`, `shield`, `shieldInstalled`, `shieldEnabled`, `warpFuel`, `alive`. Missing keys keep the current value; loaded values are clamped.

## Star system collisions (3.4)
Hitting a `planet` or `moon` uses the tiered speed damage (`ship.damage_planet`, default 50 at 200 m/s, minimum 1 HP) and bounces off the closing speed the physics world reports (relative to the orbiting body).
Hitting the `sun` is instant death: `DamageTaken{source "sun"}` then `Died{"sun"}` (a full shield cannot absorb it), and the respawn module puts the ship back at the spawn point. `ship.sun_kills` (default true); set it to false and the sun only hurts like a planet.
At warp speed (5000 m/s, up to 15,000 with drive upgrades; see docs/WARP.md) the swept test does not tunnel: a warp crash into a planet reports ~5000 m/s closing and about 1,250 damage (fatal). At `engine.log_level: debug` every hit logs kind, radius, closing speed and damage.

## Chase view
The V key switches between cockpit view and a rigid chase camera (`core/camera`: same orientation as the ship, offset behind and above in the ship frame, so the sky never changes). In chase view the real ShipV2 model is drawn from outside by `ship/cockpit`
(docs/COCKPIT.md, "Chase view"); `ship_core` draws its wireframe fighter only when `cockpit::IShipModel` says the real one is not drawn.

## Scrapes, resting contact and the contact cooldown (bug fix 2.1c)
**The bug:** holding a direction against a surface at low speed drained HP in seconds (rock: dead in ~6 s, about 20 hits per second). Cause: `onCollided` pushes the ship out of the surface and bounces it, but the player keeps thrusting, so the hull re-enters the surface a few steps later;
the physics world reports a new "start touching" `Collided` for each re-entry, and every event applied `collisionDamage`, which is at least `ship.damage_min` (1 HP) whatever the speed, so ~20 events/s = ~20 HP/s (up to 60 if it re-entered every step).
**The fix** (pure rules in `ship_rules.h`: `contactDamage`, `velocityAfterContact`, `ContactCooldown`; unit-tested):
* **Scrape threshold** `ship.damage_min_speed` (default 3 m/s): a touch with a closing speed below it does no damage, plays no impact sound and does not bounce. The pushout stays, so the ship still cannot tunnel.
* **Resting contact:** after the pushout a scrape only removes the velocity component into the surface (relative to the other body, which may be orbiting): the ship slides along the surface instead of re-entering it every step. A real impact (>= the threshold) bounces exactly as before.
* **Contact cooldown** `ship.contact_cooldown` (default 0.6 s): the same body (rock, planet, moon, station) can damage the ship only once per cooldown, even at high speed, so a bounce straight back into it cannot chain-hit. The bounce itself still happens.
* Unchanged: the damage tiers and speed formula above the threshold (a 100 m/s hit on a planet is still 25 HP), the 1 HP minimum for hits above the threshold, the sun (lethal at any speed with `ship.sun_kills`), warp collisions, and docking (a held ship ignores station hits).
Dev aid: `--hold=thrust:1[,strafe:0.5,lift:-1,pitch,yaw,roll]` holds those axes at a value without a keyboard (saved-game tests); at `engine.log_level: debug` ship_core logs each hit and "contacts in the last second: N hits, X damage, hp" once a second.
Measured (held thrust into a static rock / a planet surface for 9 s from a saved game): before, HP 86.6 -> 54.7 -> 33.9 -> 13.1 -> destroyed by second 6 (rock; planet similar: 74 -> 55 -> 38 -> 24 -> 12 -> 6); after, one or two real impacts on the way in (13.6 + 4.7 HP on the rock, 19.2 + 6.7 on the planet) and then nothing: the ship slides.
