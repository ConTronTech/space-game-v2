# Physics research: real space physics, and what to take (docs/VISION.md "Research task")

Written 2026-09-21 while testing was paused, per docs/VISION.md: *"Study how real space physics works... Take inspiration, then remove what does not
work, judged by the user's hand-testing... This is computationally expensive, so optimisation is absolutely necessary."* This is a research note, not a
spec: it ends in a recommendation and answers open question #14 (docs/QUESTIONS.md, "real gravity"), but nothing here is built yet.

## 1. What the game already has right
- **No drag, no speed cap, full Newtonian ship movement.** This alone is the single biggest thing that makes space combat *feel* like space combat:
  momentum keeps carrying you, turning does not slow you down, and "coasting" is a real state. Real spacecraft dogfighting, to the extent it has been
  seriously analysed, is dominated by exactly this: long Newtonian coasts "punctuated by relatively short periods of maneuvering" around discrete
  engine burns ([Gizmodo, "The Physics of Space Battles"](https://gizmodo.com/the-physics-of-space-battles-5426453)). Games that got this right
  ([Orbital Dogfight](https://mogacreative.itch.io/orbital-dogfight), [Orbital Racer](https://plugindigital.itch.io/orbital-racer)) treat understanding
  Newtonian motion as the main *skill*, not a setting. That already matches this project's design principle (VISION.md: "everyone gets the same
  treatment").
- **Fixed-step simulation + interpolated rendering.** This is the standard way to keep physics deterministic and stutter-free regardless of frame
  rate, and it is already in place (`onFixedUpdate`, `alpha()`).
- **Analytic (not integrated) orbits for the star system.** `world::updatePositions` computes a body's position directly from simulation time
  (`pos = f(t)`), so there is no numerical drift and no per-step cost beyond evaluating the formula. This is the right call for bodies that do not
  interact with anything except the ship.
- **Orbit lock as an assist, not automatic gravity.** The ship is not pulled by anything today; `ship/orbit_lock` computes what a circular orbit at the
  current altitude would look like and only locks the ship onto it when the player aligns. This is closer to a "SAS" / autopilot assist than to a
  physics feature, and it is cheap: one `sqrt` and a bit of vector math per lock, no simulation.

## 2. How real orbital mechanics actually gets modelled in games and in mission design
There are two established approaches, and real space agencies use **both**, for different purposes:

### Patched-conic approximation (what most space games use)
Each body gets a **sphere of influence** (SOI): inside it, only that body's gravity is considered, and the two-body problem inside an SOI has an exact
closed-form solution (a conic section: ellipse, parabola or hyperbola) — no integration needed, ever. A ship's trajectory is a sequence of clean conic
arcs "patched" together at SOI boundaries. The classic SOI radius formula is `r_SOI = a * (m / M)^(2/5)`, where `a` is the smaller body's orbital
distance from the larger one and `m`/`M` are their masses ([sphere-of-influence background](https://academic.oup.com/mnras/article/391/2/675/1746757)).
- **Kerbal Space Program** uses exactly this. Consequence, confirmed by KSP's own community: "orbits won't decay as long as you are above a body's
  atmosphere," Lagrange points are impossible (spacetime is not modelled, only pairwise two-body patches), and SOIs are perfect spheres, not the
  lopsided real ones ([KSP forum discussion](https://forum.kerbalspaceprogram.com/topic/124639-what-are-patched-conics/)). Despite the simplifications,
  it is widely regarded as *the* reference for "orbital mechanics done right" in a game, because Hohmann transfers, gravity assists and rendezvous all
  work the way a player expects, and it costs almost nothing at runtime — a ship not under thrust just evaluates its conic formula.
- **NASA's own workflow mirrors this**: mission planners start with the patched-conic approximation for a fast "napkin estimate," then switch to a
  full numerical N-body integration only when the real trajectory needs to be pinned down precisely
  ([Wikipedia, "Patched conic approximation"](https://en.wikipedia.org/wiki/Patched_conic_approximation)).

### Full N-body simulation
Every body pulls on every other body, every step, via direct summation (`O(n²)` pairs) or, for larger `n`, the **Barnes-Hut** tree approximation, which
groups distant bodies into a single pseudo-mass and cuts the cost to `O(n log n)` ([Barnes-Hut overview](https://beltoforion.de/en/barnes-hut-galaxy-simulator/),
[complexity comparison](https://github.com/dileban/nbody-simulation)). This is strictly more accurate — it is the only way to get orbital perturbation
(one planet's gravity slightly bending another's orbit) or Lagrange points — but it is real, recurring CPU cost, every fixed step, that patched conics
simply do not pay because the conic formula is evaluated in closed form.

**Numerical integrator choice matters for N-body.** A naive integrator like RK4 is *accurate* but not *symplectic*: it does not conserve energy, so a
simulated orbit slowly and monotonically drifts over a long run — invisible over a few thousand steps, but "the energy slowly, monotonically drifts"
over millions of steps. A symplectic integrator (leapfrog / Verlet family) instead lets the energy **oscillate around the true value** rather than
trend away from it, at similar cost per step ([RK4 vs symplectic drift, DEV Community](https://dev.to/iwtlp/why-your-physics-sim-drifts-and-when-rk4-is-the-wrong-fix-4dp3),
[MNRAS on symplectic integration](https://academic.oup.com/mnras/article/414/1/659/1097134)). If this project ever integrates N-body motion instead of
using closed-form analytic orbits, the integrator must be symplectic (e.g. velocity Verlet / leapfrog), not RK4, or planets will visibly wander from
their starting orbits over a long play session.

## 3. What this means for THIS game
The project's own two hardest constraints are (a) the laptop has two weak cores and no AVX, so anything paid **every fixed step for every body** is
expensive, and (b) the star system already generates 15+ bodies (`good-20260921-25` laptop log) plus up to hundreds of asteroids. A full N-body
simulation of that is not affordable on the target hardware, and — per the KSP precedent — is not actually necessary for the game to feel right.

**Recommendation: stay analytic for planetary motion (no change), add REAL gravity as a force on the ship only, patched-conic style (single dominant
body, sphere of influence), not full N-body.**

Concretely, if/when gravity is added (question #14):
1. **The planets/moons/sun keep moving on their existing closed-form analytic orbits.** They do not need to be pulled by anything, because nothing
   perturbs them in this design (no other ships are massive enough to matter, and player-visible perturbation between planets is not a stated goal).
   This keeps the existing zero-drift, zero-per-step-cost system for the *world*.
2. **The ship gets a gravity acceleration from exactly one body: whichever one it is inside the sphere of influence of**, using the real SOI formula
   from section 2 (already have every body's radius, position and — for the sun — the total system mass to approximate `M`). Outside every SOI, gravity
   is zero. This is `O(1)` per fixed step, not `O(bodies)`, because only the nearest/dominant body needs to be checked (a good SOI test doubles as the
   existing "nearest body" logic `ship/orbit_lock` already has, so most of the code exists).
3. **This turns `ship/orbit_lock`'s guide from a display into physics.** Today the guide *shows* what circular orbit the ship would have; with real
   gravity, flying tangentially at the right speed inside an SOI produces that orbit *for free*, because gravity now actually curves the path. The lock
   button becomes what SAS is in KSP: a convenience that holds a stable orbit precisely, not a teleport. This is a small, well-scoped change because
   the orbit math (circular-orbit speed, tangent, plane) is already written and tested (`orbit_lock_rules.h`).
4. **No SOI can be perfectly spherical in reality, but treating it as one (as KSP does) is the correct trade for this project**: it is a known,
   accepted simplification, it is cheap, and the "warts" (no Lagrange points, no perturbation, a sharp gravity cutoff at the SOI edge) are the same warts
   players of the reference game (KSP) already understand and forgive.
5. **Do not add N-body / Barnes-Hut** unless a specific feature needs it (e.g. a future "gravity assist" mechanic that must see a *third* body's pull
   during a flyby). If that need appears, Barnes-Hut is the correct algorithm (`O(n log n)`, well documented, moderate implementation cost) rather than
   direct summation, but it should be profiled and gated behind a quality preset (Low = off) exactly like the asteroid field and planet LOD already
   are, because it is real per-step CPU cost the laptop cannot spare by default.
6. **If gravity ever needs to be integrated rather than evaluated in closed form** (true for the ship inside an SOI, since the ship's own thrust makes
   its path non-conic in general — actually inside a single SOI with no thrust it IS a conic and can still be closed-form; only *while thrusting* does
   it need small-step numerical integration), use a symplectic integrator (semi-implicit/leapfrog: update velocity from acceleration, then position from
   the new velocity) rather than RK4, so a long unthrusted coast does not visibly gain or lose energy over a play session. This costs one extra vector
   add over naive Euler and is not meaningfully more expensive.

## 4. What to explicitly NOT copy from "hard sci-fi" space combat, because it would hurt the game
Research into "realistic" space combat consistently surfaces the same uncomfortable facts, several of which conflict with a fun, testable, moddable
game and should be deliberately rejected:
- **Real space combat, if it happened, would mostly be over before either side reacts** (detection ranges vs. weapon travel time), which is not
  interesting gameplay. The existing design (finite bolt speed and lifetime, so the player can dodge/lead) already deliberately keeps combat
  humanly-reactable rather than modelling instant light-speed detection — keep it that way.
- **True 6-degree-of-freedom "face the threat while burning the other way" flying** (Babylon 5 Starfuries, Battlestar Galactica Vipers) is realistic
  and is *already* how this game's ship moves (thrust and orientation are independent, no drag). No further change needed there; this is already the
  differentiator from arcade flight models.
- **Delta-v budgets / propellant mass fraction** (a real ship has a fixed, small amount of fuel and every burn is a hard trade-off, governed by the
  Tsiolkovsky rocket equation) is real and does map onto this game's existing warp-fuel-is-finite, mining-refills-it design — that is already the
  right level of "realistic resource pressure" without needing rocket-equation mass tracking for ordinary manoeuvring thrust, which would be a large
  complexity increase for little player-facing benefit given the ship does not lose mass by burning normal RCS-style thrust in this design.

## 5. Performance budget (target hardware: docs/VISION.md, i5 M 560, 2c/4t, no AVX, Ironlake iGPU)
- Per-body analytic orbit evaluation (current system): a handful of trig calls per body per fixed step; 15+ bodies measured at effectively free.
- A single-body SOI gravity term for the ship: one vector subtract, one `1/r²` and a scale — `O(1)`, not `O(bodies)`, because only the current dominant
  body applies. This is cheaper than the existing collision sweep against nearby asteroids and should not be separately budgeted.
- Anything beyond that (N-body between planets, perturbation, more than one gravitating body acting on the ship at once) should be treated the same
  way `world/asteroids` and `world/planet_mesh` already are: gated behind `core/quality` presets, profiled with the existing `--profile`/live profiler
  (F3) before merging, and benchmarked on the real laptop (`package/tools/laptop_bench.sh`) — not assumed safe.

## 6. Answer to docs/QUESTIONS.md #14 ("should planets/moons/the sun exert real gravity on the ship?")
**Recommendation: yes, but scoped** — single-dominant-body (sphere-of-influence) gravity on the ship only, planets/moons/sun keep their existing
zero-cost analytic motion untouched, `orbit_lock` becomes a real-physics assist rather than a display, and full N-body/perturbation is explicitly out
of scope unless a specific future feature needs it. This is the KSP-proven middle ground: it changes flight meaningfully (the user's own framing:
"Space-Engineers-like planetary gravity vs pure space") without the computational cost or complexity of full N-body, and it reuses code that already
exists (`orbit_lock_rules.h`'s nearest-body and circular-orbit math). Left for the user to confirm before any implementation spec is written, since it
changes core flight feel.

## Sources
- [Patched conic approximation (Wikipedia)](https://en.wikipedia.org/wiki/Patched_conic_approximation)
- [Fun with Orbital Mechanics - Children of a Dead Earth](https://childrenofadeadearth.wordpress.com/2016/05/17/fun-with-orbital-mechanics/)
- [What are patched conics? - KSP forum](https://forum.kerbalspaceprogram.com/topic/124639-what-are-patched-conics/)
- [Kerbal Space Program - Wikipedia](https://en.wikipedia.org/wiki/Kerbal_Space_Program)
- [The Physics of Space Battles - Gizmodo](https://gizmodo.com/the-physics-of-space-battles-5426453)
- [Orbital Dogfight (itch.io)](https://mogacreative.itch.io/orbital-dogfight)
- [Orbital Racer (itch.io)](https://plugindigital.itch.io/orbital-racer)
- [Sphere of influence and gravitational capture radius - MNRAS](https://academic.oup.com/mnras/article/391/2/675/1746757)
- [Why your physics sim drifts, and when RK4 is the wrong fix - DEV Community](https://dev.to/iwtlp/why-your-physics-sim-drifts-and-when-rk4-is-the-wrong-fix-4dp3)
- [Global Symplectic Integrator - MNRAS](https://academic.oup.com/mnras/article/414/1/659/1097134)
- [The Barnes-Hut Galaxy Simulator](https://beltoforion.de/en/barnes-hut-galaxy-simulator/)
- [Parallel N-Body Simulation with Barnes-Hut Approximation (GitHub)](https://github.com/dileban/nbody-simulation)

## Implemented (3.5e, 2026-09-21)
The section 3 / #14 recommendation is in: `ship/gravity` (docs/GRAVITY.md) applies single-dominant-body SOI gravity to the ship with a semi-implicit Euler kick through `IShip::setVelocity`, the same `mu` as the orbit guide, on by default (`ship.gravity_enabled`). Bodies stay analytic. A guide-speed orbit around Planet 1 held 610-612 altitude over 43 s.
