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

## Target hardware (the user's laptop, "crap-top", 2026-09-20)
| | |
|---|---|
| Machine | Acer Aspire 5733 |
| OS | Linux Mint 22.3 (x86_64, Cinnamon, kernel 6.14), gnome-terminal |
| CPU | Intel Core i5 M 560: 2 cores / 4 threads @ 2.67 GHz (2010-era: SSE4.2, **no AVX**) |
| GPU | Intel integrated ("Intel Core Processor" = Ironlake generation) - shares system RAM |
| RAM | 7.6 GB (about 1.1 GB in use when idle) |
| Displays | 1366x768 (main) and 1280x1024 |

What this means for the code (rules until measured otherwise):
1. **Stay on OpenGL 2.1 fixed-function** (what the game requests today). Ironlake is believed to top out around GL 2.1; do not move to GL 3+/shaders that need it.
   The game will log the real renderer, GL version and max texture size at startup (`logs/game.log`) so this can be confirmed on the laptop.
2. **Generic x86-64 builds only.** Never add `-march=native` (or AVX) to a build that is meant for the laptop or for release.
3. **Two real cores, weak GPU:** keep per-frame CPU work small (no per-frame allocations, cache what can be cached), watch driver-heavy calls such as the cockpit's
   `glPushAttrib(GL_ALL_ATTRIB_BITS)` if profiling points at them, and budget triangles/draw calls for the world (planets, asteroids) before adding them.
4. **Memory/textures:** the old skybox faces are 4-8 MB PNGs each (huge textures). They must be downscaled/capped when loaded (Phase 3.1), since graphics memory is shared.
5. **Resolutions:** support 1366x768 and 1280x1024 (5:4) properly; the HUD scales by `min(w/1280, h/720)`, so check both.
6. **Budget:** aim for 60 fps at 1280x720 on this laptop, with 30 fps as the floor. A `--benchmark` mode (frame-time stats) is planned so the user can run it on the laptop and paste the result.

## Performance target
Smooth on the user's laptop (specs above). Measure it before big simulation features land: benchmark mode + startup hardware log come before the heavy world work.
