# Vision and release plan

Written from the user's own words (2026-09-20). This is direction, not a schedule.

## What the game is
A **hard, strategic** space game: "you're in space, use your tools wisely". Realistic movement (Newtonian, no drag, no speed cap), survival pressure,
exploration, mining, crafting. It is **not** meant to be an easy game and **not** meant to be dopamine-filling fun. It is a passion project and should be
**completely customisable** (drop-in modules, JSON data, config; see docs/MODULES.md, DATA.md, CONFIG.md).

## Release plan (later)
- Release the **source on GitHub** and a **dedicated build package on itch.io** as the playable game.
- The itch.io page must **warn clearly that this is NOT an easy game**: it is strategic, not a reward-loop game.
- The user will feel happy releasing when it **runs smoothly standalone on their laptop** (a dedicated testing rig may come later) and, **if** LAN/networking
  (server + client) is added, when that works well too.
- Related: Linux only "for now" (see the parked browser / mobile ideas in docs/ROADMAP.md).

## Design principle: everyone gets the same treatment
Space is unforgiving and the rules apply to **everything**, not just the player. The player has no uncapped-speed awareness helpers and no special physics;
**NPCs and drones follow the same rules** (same movement, same limits, same handicaps). Dogfighting in space is realistically very hard, so combat is difficult
for the player and for NPCs alike.

## Research task (later): real space physics
Study how real space physics works, since the game is effectively a gargantuan simulation of a solar system. Take inspiration, then **remove what does not work**,
judged by the user's hand-testing or the coordinator's tests, so the physics stays correct **and fun to play**. This is computationally expensive, so **optimisation
is absolutely necessary**: budget for it (spatial structures, level of detail, fixed-step interpolation already exist; profile before adding more).

## Performance target
Smooth on the user's laptop. Measure it before big simulation features land (planned: a small benchmark scene + frame-time log).
