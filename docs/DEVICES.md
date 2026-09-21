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
| stick shifter | - | - | waiting for an adapter |
| pedals | - | - | to be connected |

`/dev/input/js*` are world-readable (SDL joystick works without the `input` group); `event*` need the `input` group (the user is not in it).

## Plan
Monitor awareness (display selection, resolution/mode list, aspect-aware UI and FOV, refresh) and a joystick / wheel / pedal / shifter input method with per-device JSON profiles: see docs/ROADMAP.md (Phase 1.8 / 1.9).
