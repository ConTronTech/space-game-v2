# UI

`core/ui_handler` draws the 2D overlay. Modules register **panels** (ordered draw callbacks) and use the
glass toolkit. Pixel coordinates, origin top-left.

```cpp
auto& ui = eng.services.require<core::UIHandler>();
ui.addPanel("my_hud", 10, [](core::UIHandler& ui) {      // order: low = behind, high = on top
    ui.glass(16, 16, 200, 58);                             // frosted translucent panel
    ui.text(32, 22, "SPEED", 12, ui.theme.textDim);
    ui.textCentered(cx, y, "Title", 28, ui.theme.text);
    if (ui.button("Resume", x, y, w, h, /*focused*/ false)) { ... }        // true on click
    bool on = ui.toggle("Fullscreen", x, y, w, h, on, false);              // returns new value
    float v = ui.slider("Field of View", x, y, w, h, v, 60, 120, false);   // returns new value
});
```
Remove it in `shutdown`: `ui.removePanel("my_hud")`.

Widgets are immediate-mode: call them every frame and use the return value. `focused` is for keyboard/controller
focus; the mouse hover also highlights. They only react to the pointer while the cursor is free
(`ui.pointerFree()`), i.e. not while the mouse is captured for flight.

## Glass look
`glass()` = soft shadow + translucent gradient fill + light sheen over the top half + thin border + a glint
along the top edge. Things behind it stay visible. (It is see-through, not blurred.)

Tweak it in `config/ui/theme.json` (optional, every key optional, colors are `[r,g,b,a]` 0..1):
lower `glass_top` / `glass_bottom` alpha for more transparency, change `accent` for the highlight color,
`radius` for corner roundness. Changes apply on next launch.

Fonts: `assets/fonts/ui.ttf` if present, otherwise the system DejaVu Sans.

## Pause menu (`ui/pause_menu`)
Esc pauses the simulation (`Engine::setPaused`) and opens the menu: Resume / Settings (FOV slider, fullscreen) / Exit Game.
Mouse works, and so do the `ui_up`, `ui_down`, `ui_left`, `ui_right`, `ui_confirm` and `pause` actions from the input profile.

Pausing freezes only `onFixedUpdate`; UI, input and rendering keep running. Other modules can react:
```cpp
eng.events.subscribe<engine::PauseChanged>([](const engine::PauseChanged& e) { ... e.paused ... });
```
(the mouse module uses this to release the cursor while paused).

## Dev flags
`--paused` / `--paused=settings` start with the menu open. `--screenshot=out.bmp [--screenshot-frame=N]` saves a frame.
