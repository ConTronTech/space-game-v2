#pragma once
// ICockpitScreens: who draws the content of the '@' screens in the cockpit model.
//
// A model material named "@GROUP-NAME" (e.g. "@HUD-INFO") is not drawn as coloured geometry; ship/cockpit takes its face as a
// screen and asks the renderer registered for GROUP to draw on it. Other modules can supply content for a group without
// touching the cockpit module:
//
//     auto* screens = eng.services.get<cockpit::ICockpitScreens>();      // null if ship/cockpit is off
//     screens->registerRenderer("NAV", [](const cockpit::ScreenContext& c) {
//         c.canvas.text(0.1f, 0.1f, "NAV " + c.quad.name, 0.12f, {0.3f, 1, 0.5f});   // canvas: x 0..aspect, y 0..1 (top -> bottom)
//     });
//     ... shutdown: screens->removeRenderer("NAV");
//
// Renderers run inside the cockpit's view-space render pass, every frame the cockpit is visible, right after the model. The
// screen already has its dark backing; blending is on, lighting off. Do not change GL state beyond what the canvas does.
// The built-in "HUD" group (the ShipV2 screens FLIGHT_DATA / SHIP_SYSTEMS / PROXIMITY_RADAR) uses this same interface.
#include <functional>
#include <string>
#include "core/import_handler/obj_parser.h"
#include "ship/cockpit/screen_canvas.h"

namespace cockpit {

struct ScreenContext {
    const core::TaggedQuad& quad;   // group, name, corners... (GL space: view space)
    ScreenCanvas& canvas;           // draw here
    const std::string& content;     // what ship.json says this tag shows ("FLIGHT_DATA"), empty if it does not name the tag
    float time;                     // seconds since start, for animation
    float brightness;               // cockpit.screen_brightness (the canvas already applies it to colours)
};

using ScreenRenderer = std::function<void(const ScreenContext&)>;

class ICockpitScreens {
public:
    virtual ~ICockpitScreens() = default;
    // One renderer per group; registering again replaces it (so a mod can take over "HUD"). Group names are case-sensitive.
    virtual void registerRenderer(const std::string& group, ScreenRenderer fn) = 0;
    virtual void removeRenderer(const std::string& group) = 0;
    // Should the flat 2D HUD overlay draw? False only while the model's own screens are visible (ship.json showDefaultUI=false
    // and the cockpit is drawing); true in chase view, with the cockpit off, or when the model has no screens.
    virtual bool showsDefaultUI() const = 0;
};

} // namespace cockpit
