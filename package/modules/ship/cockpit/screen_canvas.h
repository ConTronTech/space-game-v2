#pragma once
// Drawing surface for one cockpit screen (a tagged quad in the ship model). All coordinates are in "screen heights":
// x runs 0..aspect() left -> right, y runs 0..1 top -> bottom, so a circle stays round and sizes are independent of how
// big the quad is in the model. Drawing calls do NOT touch OpenGL: they append coloured quads (view-space positions) to a
// ScreenMesh, which the cockpit draws with ONE array draw per screen and keeps between redraws (cockpit.screen_hz).
// Header-only and GL-free, so it is unit-tested (tests/test_cockpit.cpp).
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "core/import_handler/obj_parser.h"
#include "ship/cockpit/stroke_font.h"

namespace cockpit {

struct ScreenColor {
    float r = 1, g = 1, b = 1, a = 1;
};

// Coloured quads, four vertices each, in draw order. positions: xyz per vertex, colors: rgba per vertex.
struct ScreenMesh {
    std::vector<float> positions, colors;
    size_t vertexCount() const { return positions.size() / 3; }
    void clear() { positions.clear(); colors.clear(); }       // keeps the capacity: rebuilding a screen does not reallocate
};

class ScreenCanvas {
public:
    // 'brightness' scales the rgb of everything drawn (tunable cockpit.screen_brightness). The mesh is appended to, not cleared.
    ScreenCanvas(const core::TaggedQuad& quad, float brightness, ScreenMesh& out)
        : quad_(quad), out_(out), aspect_(quad.height > 1e-6f ? quad.width / quad.height : 1.0f), brightness_(brightness) {}

    float aspect() const { return aspect_; }                 // quad width / height
    const core::TaggedQuad& quad() const { return quad_; }

    void fill(const ScreenColor& c) {                         // the whole screen (the dark backing; draws first)
        for (int i : {0, 1, 2, 3}) push(quad_.corners[i].x, quad_.corners[i].y, quad_.corners[i].z, c);
    }
    void rect(float x, float y, float w, float h, const ScreenColor& c) {
        P a = at(x, y), b = at(x + w, y), d = at(x + w, y + h), e = at(x, y + h);
        push(a, c); push(b, c); push(d, c); push(e, c);
    }
    void frame(float x, float y, float w, float h, float t, const ScreenColor& c) {
        strip(x, y, x + w, y, t, c);
        strip(x + w, y, x + w, y + h, t, c);
        strip(x + w, y + h, x, y + h, t, c);
        strip(x, y + h, x, y, t, c);
    }
    void line(float x1, float y1, float x2, float y2, float thickness, const ScreenColor& c) { strip(x1, y1, x2, y2, thickness, c); }
    void circle(float cx, float cy, float r, float t, const ScreenColor& c, int seg = 48) {
        for (int i = 0; i < seg; i++) {
            float a1 = 6.2831853f * (float)i / (float)seg, a2 = 6.2831853f * (float)(i + 1) / (float)seg;
            strip(cx + std::cos(a1) * r, cy + std::sin(a1) * r, cx + std::cos(a2) * r, cy + std::sin(a2) * r, t, c);
        }
    }
    void bar(float x, float y, float w, float h, float frac, const ScreenColor& fillColor) {   // dark trough + coloured fill + border
        rect(x, y, w, h, {0.06f, 0.08f, 0.09f, 1});
        if (frac > 0) rect(x, y, w * std::min(frac, 1.0f), h, fillColor);
        frame(x, y, w, h, h * 0.10f, {0.35f, 0.5f, 0.55f, 1});
    }

    // Text with its top-left at (x, y); 'height' is the cell height. Thickness defaults to a bold stroke.
    void text(float x, float y, const std::string& s, float height, const ScreenColor& c, float thickness = 0) {
        if (s.empty()) return;
        if (thickness <= 0) thickness = height * 0.13f;
        static thread_local std::vector<Stroke> strokes;      // scratch, reused: no allocation per text call
        strokes.clear();
        layoutText(s, x, y, height, strokes);
        for (auto& k : strokes) strip(k.x1, k.y1, k.x2, k.y2, thickness, c);
    }
    void textCentered(float cx, float y, const std::string& s, float height, const ScreenColor& c, float thickness = 0) {
        text(cx - cockpit::textWidth(s, height) * 0.5f, y, s, height, c, thickness);
    }
    static float textWidth(const std::string& s, float height) { return cockpit::textWidth(s, height); }

private:
    struct P { float x, y, z; };
    P at(float x, float y) const {
        engine::Vec3 p = quad_.at(aspect_ > 0 ? x / aspect_ : 0, y) + quad_.normal * lift_;   // a hair in front of the backing: no z-fighting
        return {p.x, p.y, p.z};
    }
    void push(const P& p, const ScreenColor& c) { push(p.x, p.y, p.z, c); }
    void push(float x, float y, float z, const ScreenColor& c) {
        out_.positions.insert(out_.positions.end(), {x, y, z});
        out_.colors.insert(out_.colors.end(), {std::clamp(c.r * brightness_, 0.0f, 1.0f), std::clamp(c.g * brightness_, 0.0f, 1.0f),
                                              std::clamp(c.b * brightness_, 0.0f, 1.0f), c.a});
    }
    void strip(float x1, float y1, float x2, float y2, float t, const ScreenColor& c) {   // a thick line as one quad
        float dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-6f) { dx = 1; dy = 0; len = 1; }         // a dot: draw a small square
        dx /= len; dy /= len;
        float h = t * 0.5f, px = -dy * h, py = dx * h;
        x1 -= dx * h; y1 -= dy * h; x2 += dx * h; y2 += dy * h;   // square caps close the gaps at joints
        push(at(x1 + px, y1 + py), c); push(at(x1 - px, y1 - py), c); push(at(x2 - px, y2 - py), c); push(at(x2 + px, y2 + py), c);
    }

    const core::TaggedQuad& quad_;
    ScreenMesh& out_;
    float aspect_ = 1;
    float brightness_ = 1;
    float lift_ = 0.0015f;   // metres in front of the quad
};

} // namespace cockpit
