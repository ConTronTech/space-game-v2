# Physics world (collisions)

`core/physics_world` provides `core::IPhysics`: sphere bodies in a spatial grid, with **swept** collision tests and events.
It only *detects*. The module that owns a body decides what a hit means (damage, bounce, destroy).

```cpp
auto* phys = eng.services.get<core::IPhysics>();                        // null if the module is off
core::BodyId rock = phys->addBody("asteroid", pos, radius, /*dynamic=*/false);
core::BodyId ship = phys->addBody("ship", pos, 2.5f, /*dynamic=*/true);

// each fixed step, after moving the ship:
phys->setBody(ship, pos, vel);

eng.events.subscribe<core::Collided>([&](const core::Collided& c) {
    // c.a / c.b (ids), c.kindA / c.kindB, c.normal (a -> b), c.speed (closing speed), c.posA / c.posB (at first touch), radii
});
```

- **Dynamic** bodies are movers and can hit anything; **static** bodies only get hit (static-vs-static is ignored).
- **Swept:** the path since the last step is tested, so a body at warp speed (33 m per 60 Hz step) cannot tunnel through a small rock.
The reported contact is the moment of **first touch**, so you can put the mover back just outside the other body.
- **One event per contact:** `Collided` fires when two bodies start touching, not every step they stay in contact.
- `teleport(id, pos)` moves without sweeping (loading a game, respawn, and *resolving* a collision: teleport the body outside
the thing it hit, or it would re-collide next step).
- **Big bodies** (planets, the sun) span too many grid cells and live in a short list checked against every mover.
- `query(center, radius, out)` lists overlapping bodies (O(n), for occasional use).
- Order: `core/physics_world` steps after gameplay modules have moved their bodies (priority 10); gameplay modules declare
`optionalDependencies()` on it so it exists when they `init()`.
- Tunable: `physics.cell_size` (default 200 m). Flight: `flight.hull_radius`, `flight.bounce`.

The flight demo is the reference: ship = dynamic body, rocks = static, `onCollided` bounces the ship, plays `impact`, flashes IMPACT on the HUD.

## Star system bodies
`world/star_system` registers the sun, planets and moons as static bodies (kinds `sun`, `planet`, `moon`) and moves them every fixed step with `setBody(id, pos, vel)` (not `teleport`), so a moving static body is swept correctly and `Collided::speed`
is the closing speed relative to it. The ship bounces using that closing speed, so an orbiting planet that runs into a parked ship knocks it away instead of swallowing it (contacts only fire when they begin). See docs/WORLD.md for the float-precision note at 350,000 units.
