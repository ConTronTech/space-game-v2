#pragma once
// Drawing surface for one cockpit screen (a tagged quad in the ship model). All coordinates are in "screen heights":
// x runs 0..aspect() left -> right, y runs 0..1 top -> bottom, so a circle stays round and sizes are independent of how
// big the quad is in the model. Everything is drawn as flat quads (thick lines, filled rects, stroke-font text) in the
// current view space; the cockpit pass sets up blending / no lighting before calling a renderer.
#include <string>
#include <vector>
#include "core/import_handler/obj_parser.h"
#include "ship/cockpit/stroke_font.h"

namespace cockpit {

struct ScreenColor {
    float r = 1, g = 1, b = 1, a = 1;
};

class ScreenCanvas {
public:
    // 'brightness' scales the rgb of everything drawn (tunable cockpit.screen_brightness).
    ScreenCanvas(const core::TaggedQuad& quad, float brightness);

    float aspect() const { return aspect_; }                 // quad width / height
    const core::TaggedQuad& quad() const { return quad_; }

    void fill(const ScreenColor& c);                          // the whole screen (writes depth: it is the opaque backing)
    void rect(float x, float y, float w, float h, const ScreenColor& c);
    void frame(float x, float y, float w, float h, float thickness, const ScreenColor& c);
    void line(float x1, float y1, float x2, float y2, float thickness, const ScreenColor& c);
    void circle(float cx, float cy, float radius, float thickness, const ScreenColor& c, int segments = 48);
    void bar(float x, float y, float w, float h, float frac, const ScreenColor& fill);   // dark trough + coloured fill + border

    // Text with its top-left at (x, y); 'height' is the cell height. Thickness defaults to a bold stroke.
    void text(float x, float y, const std::string& s, float height, const ScreenColor& c, float thickness = 0);
    void textCentered(float cx, float y, const std::string& s, float height, const ScreenColor& c, float thickness = 0);
    static float textWidth(const std::string& s, float height) { return cockpit::textWidth(s, height); }

private:
    struct P { float x, y, z; };
    P at(float x, float y, float lift) const;
    void setColor(const ScreenColor& c) const;
    void quadStrip(float x1, float y1, float x2, float y2, float thickness);

    const core::TaggedQuad& quad_;
    float aspect_ = 1;
    float brightness_ = 1;
    float lift_ = 0.0015f;   // metres in front of the quad, so content never z-fights with the backing
};

} // namespace cockpit
