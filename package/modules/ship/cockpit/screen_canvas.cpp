#include "ship/cockpit/screen_canvas.h"
#include <GL/gl.h>
#include <algorithm>
#include <cmath>

namespace cockpit {

ScreenCanvas::ScreenCanvas(const core::TaggedQuad& quad, float brightness)
    : quad_(quad), aspect_(quad.height > 1e-6f ? quad.width / quad.height : 1.0f), brightness_(brightness) {}

ScreenCanvas::P ScreenCanvas::at(float x, float y, float lift) const {
    engine::Vec3 p = quad_.at(aspect_ > 0 ? x / aspect_ : 0, y) + quad_.normal * lift;
    return {p.x, p.y, p.z};
}

void ScreenCanvas::setColor(const ScreenColor& c) const {
    glColor4f(std::clamp(c.r * brightness_, 0.0f, 1.0f), std::clamp(c.g * brightness_, 0.0f, 1.0f),
              std::clamp(c.b * brightness_, 0.0f, 1.0f), c.a);
}

void ScreenCanvas::fill(const ScreenColor& c) {
    setColor(c);
    glDepthMask(GL_TRUE);
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; i++) glVertex3f(quad_.corners[i].x, quad_.corners[i].y, quad_.corners[i].z);
    glEnd();
    glDepthMask(GL_FALSE);
}

void ScreenCanvas::rect(float x, float y, float w, float h, const ScreenColor& c) {
    setColor(c);
    P a = at(x, y, lift_), b = at(x + w, y, lift_), d = at(x + w, y + h, lift_), e = at(x, y + h, lift_);
    glBegin(GL_QUADS);
    glVertex3f(a.x, a.y, a.z); glVertex3f(b.x, b.y, b.z); glVertex3f(d.x, d.y, d.z); glVertex3f(e.x, e.y, e.z);
    glEnd();
}

void ScreenCanvas::quadStrip(float x1, float y1, float x2, float y2, float t) {
    float dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6f) { dx = 1; dy = 0; len = 1; }            // a dot: draw a small square
    dx /= len; dy /= len;
    float h = t * 0.5f, px = -dy * h, py = dx * h;
    x1 -= dx * h; y1 -= dy * h; x2 += dx * h; y2 += dy * h;   // square caps close the gaps at joints
    P a = at(x1 + px, y1 + py, lift_), b = at(x1 - px, y1 - py, lift_), c = at(x2 - px, y2 - py, lift_), d = at(x2 + px, y2 + py, lift_);
    glVertex3f(a.x, a.y, a.z); glVertex3f(b.x, b.y, b.z); glVertex3f(c.x, c.y, c.z); glVertex3f(d.x, d.y, d.z);
}

void ScreenCanvas::line(float x1, float y1, float x2, float y2, float thickness, const ScreenColor& c) {
    setColor(c);
    glBegin(GL_QUADS);
    quadStrip(x1, y1, x2, y2, thickness);
    glEnd();
}

void ScreenCanvas::frame(float x, float y, float w, float h, float t, const ScreenColor& c) {
    setColor(c);
    glBegin(GL_QUADS);
    quadStrip(x, y, x + w, y, t);
    quadStrip(x + w, y, x + w, y + h, t);
    quadStrip(x + w, y + h, x, y + h, t);
    quadStrip(x, y + h, x, y, t);
    glEnd();
}

void ScreenCanvas::circle(float cx, float cy, float r, float t, const ScreenColor& c, int seg) {
    setColor(c);
    glBegin(GL_QUADS);
    for (int i = 0; i < seg; i++) {
        float a1 = 6.2831853f * (float)i / (float)seg, a2 = 6.2831853f * (float)(i + 1) / (float)seg;
        quadStrip(cx + std::cos(a1) * r, cy + std::sin(a1) * r, cx + std::cos(a2) * r, cy + std::sin(a2) * r, t);
    }
    glEnd();
}

void ScreenCanvas::bar(float x, float y, float w, float h, float frac, const ScreenColor& fillColor) {
    rect(x, y, w, h, {0.06f, 0.08f, 0.09f, 1});
    if (frac > 0) rect(x, y, w * std::min(frac, 1.0f), h, fillColor);
    frame(x, y, w, h, h * 0.10f, {0.35f, 0.5f, 0.55f, 1});
}

void ScreenCanvas::text(float x, float y, const std::string& s, float height, const ScreenColor& c, float thickness) {
    if (s.empty()) return;
    if (thickness <= 0) thickness = height * 0.13f;
    std::vector<Stroke> strokes;
    layoutText(s, x, y, height, strokes);
    setColor(c);
    glBegin(GL_QUADS);
    for (auto& k : strokes) quadStrip(k.x1, k.y1, k.x2, k.y2, thickness);
    glEnd();
}

void ScreenCanvas::textCentered(float cx, float y, const std::string& s, float height, const ScreenColor& c, float thickness) {
    text(cx - cockpit::textWidth(s, height) * 0.5f, y, s, height, c, thickness);
}

} // namespace cockpit
