# Open questions for the user (saved while working; answer when back)

Newest first. Each has a default that is being used until answered.

1. **Warp key and exit speed** - warp is `Z`; leaving warp cuts speed to 200 m/s (`warp.exit_speed`, 0 = keep momentum = pure Newtonian). Keep the cut? (default: keep)
2. **Respawn** - 3 s countdown at the spawn point after death (`respawn.seconds`). OK? (default: 3 s)
3. **Orbit lock** - `O`, releases on thrust/brake/damage/warp. Should it also hold while the ship rotates freely? (default: yes, rotation allowed) Should thrust really release it, or should it need a second `O`? (default: thrust releases)
4. **Demo rocks** are off by default now (real asteroids replace them). Want the old 150 rocks near the spawn back as a training field? (default: off)
5. **Default belt density** is sparse (about 9 rocks within 1,500 units at the densest spot). Denser for gameplay? (default: `asteroids.belt_asteroids` 1500)
6. **Stations** - the default seed puts both stations on Planet 2 as surface stations. Want an orbital one guaranteed? (default: seed decides; `stations.seed_offset` re-rolls)
7. **Dock key label** in the HUD is fixed "G" (IInput exposes no bindings). Fine until key rebinding UI exists?
8. **Laptop target** - is 60 fps average / 30 fps floor at 1280x720 on Low still the goal, or is 30-40 fps acceptable for the laptop? (default: keep chasing 60 via render scale)
9. **Old game radar** - the reference tree's radar had no height stems; the stems were built from the user's description. Anything else you remember about it?
10. **Laptop folder** - `~/Documents/Space-Game-V2/space-game-v2` on the laptop holds the built game (no game.json/settings copied). Remove it when we are done?
11. **Crafting location** - should crafting need a docked station (a workbench) in the real game? Today you can craft anywhere (`crafting.require_dock false`); `true` makes the CRAFTING tab show "Dock at a station to craft" and refuse everywhere else. (default: craft anywhere)
12. **Game menu and the ship** - the I menu does not pause the game (the ship keeps flying and can be hit; `menu.pause_game true` pauses it). Keep it live? Also, the menu's tab keys are the `ui_left`/`ui_right` actions, which include A/D (strafe): pressing A/D while the menu is open switches tabs AND strafes. Want dedicated tab keys (for example `[` and `]`)? (default: live menu, ui_left/ui_right)
13. **Ore scanner and missiles** - crafting them works but USING them is refused ("not available yet") because nothing implements ore labels or missiles yet. Should the ore scanner be a permanent flag saved with the cargo (the inventory module would need a small addition)? (default: refused until the feature exists)
