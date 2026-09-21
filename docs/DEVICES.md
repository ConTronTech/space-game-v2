# Test devices (the retro rig)

Recorded 2026-09-21 from the laptop ("crap-top") over SSH. Update when hardware changes.

## Displays (`xrandr`)
| Output | Mode | Size | Notes |
|---|---|---|---|
| LVDS-1 | 1366x768 @ 60 (70 MHz) | 344x194 mm | laptop panel, primary; also offers 1280x720, 1024x768, 800x600, ... down to 320x240 |
| VGA-1 | 1280x1024 (108 MHz) | 310x230 mm | **the CRT**; extended desktop right now: 2646x1024 total (VGA at 0,0; laptop panel at 1280,256) |

Consequences: the game must pick WHICH display, must cope with 5:4 / 4:3 (and 16:9 / 16:10) aspect ratios, low-resolution and high-refresh CRT modes, and a window that opens on the wrong display must be avoidable.

## Controllers (`/proc/bus/input/devices`, `lsusb`)
| Device | USB id | Nodes | Notes |
|---|---|---|---|
| PXN PXN-V10 Wheel | 11ff:3245 | js0, event5 (+ event11 as a keyboard/mouse-like interface) | racing wheel |
| Microsoft SideWinder Joystick | 045e:003c | js1, event12 | classic joystick |
| stick shifter | - | - | waiting for an adapter; **plugs into the wheel base**, so it will appear as extra buttons/axes on the PXN wheel device |
| pedals | - | - | **plug into the wheel base** too: the wheel is ONE USB device exporting steering, pedal axes (throttle/brake/clutch) and the shifter buttons |

`/dev/input/js*` are world-readable (SDL joystick works without the `input` group); `event*` need the `input` group (the user is not in it).

## Plan
Monitor awareness (display selection, resolution/mode list, aspect-aware UI and FOV, refresh) and a joystick / wheel / pedal / shifter input method with per-device JSON profiles: see docs/ROADMAP.md (Phase 1.8 / 1.9).

## What the display work learned (phase 1.8)
* The game now lists displays itself: `./space_game_v2 --list-displays` (or `=json`) exits before a GL window exists, so it works over SSH with `DISPLAY=:0`. Use it first on the rig; expected: display 0/1 = VGA-1 (1280x1024, 5:4, `[retro/CRT-like]`) and LVDS-1 (1366x768, 16:9) in some order.
* Display numbers are SDL's 0-based indices, in the order SDL reports them (not necessarily xrandr's); on this dev PC three monitors report `KG241Y S 24" 1920x1080 @120`, `DELL SE198WFP 1440x900`, `HP 2010 1600x900` with the primary at x = 1600. Bounds are desktop coordinates: a display's origin is not (0,0) (the laptop panel at (1280,256) in the rig), so the window is centred with `SDL_WINDOWPOS_CENTERED_DISPLAY(n)`.
* `video.mode=exclusive` really switches the mode (a CRT wants this at 640x480 .. 1024x768): it picks the closest of the display's own modes, so an unsupported request is corrected and logged instead of failing. `borderless` keeps the desktop mode and skips the compositor.
* Windowed at 5:4 / 4:3 works without letterboxing: the field of view is held horizontally (Hor+), the HUD and menus scale with `min(w/1280, h/720)`.

**One device, many controls (user, 2026-09-21):** pedals and the shifter connect through the steering wheel, so the input layer must handle a single joystick device with many axes (steering, throttle, brake, clutch, maybe combined pedal axes) and buttons (shifter gears, wheel buttons, hat). Profiles are per DEVICE with axis-by-axis mappings, calibration (min/max/centre), dead zone, invert, and an optional combined-pedals mode (one axis = throttle above centre, brake below).

## Shipped controller profiles (phase 1.9) - UNVERIFIED
`config/input/devices/*.json`, matched automatically by USB id (then by name). **Both are first guesses from general knowledge of the devices and were NOT tested against the real hardware** (this dev PC has none; the logic is proven with simulated devices in `test_joystick.cpp`). Every profile says `"status": "UNVERIFIED: run --joystick-calibrate"`. The first job on the rig: `--joystick-monitor` and `--joystick-calibrate` (docs/CONTROLLERS.md), then correct the axis / button numbers.

| Device | Profile | First guess |
|---|---|---|
| Microsoft SideWinder Joystick `045e:003c` | `045e_003c_sidewinder.json` | axis 0 X -> roll, axis 1 Y -> pitch (pulled back = nose up), axis 2 twist -> yaw (inverted), axis 3 slider -> thrust (inverted); trigger = fire, buttons 1-7 = weapon_1, weapon_2, dock, toggle_warp, toggle_orbit_lock, toggle_menu, camera_next |
| PXN V10 wheel `11ff:3245` | `11ff_3245_pxn_v10.json` | axis 0 steering -> yaw (inverted, curve 1.5, dead zone 0.04); pedals: axis 2 throttle -> thrust, axis 3 brake -> brake, axis 1 clutch -> lift down (all "rest at max": a guess); buttons 0-7 = fire, dock, toggle_menu, camera_next, toggle_warp (paddle), toggle_orbit_lock (paddle), weapon_1, weapon_2; shifter buttons 12-14 = toggle_warp, toggle_orbit_lock, toggle_menu (gear numbering is a guess; a commented-out `throttle_limit` button group shows the discrete-gear format) |
| any other device | generic fallback | maps NOTHING (so it can never make the ship spin) and logs a line telling you to run the two flags |

Shifter and pedals will plug into the wheel base later: they show up as more axes and buttons of the SAME device, so only this device's profile changes.
