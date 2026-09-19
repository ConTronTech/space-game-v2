#include "core/ui_handler/ui_handler.h"
#include <GL/gl.h>
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include "core/window/window.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "engine/json.h"

namespace core {

static const char* kFontCandidates[] = {
    "assets/fonts/ui.ttf", // drop your own font here to override
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
};

static Color scaleAlpha(Color c, float k) { c.a *= k; return c; }
static Color mix(const Color& a, const Color& b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

bool UIHandler::init(engine::Engine& eng) {
    window_ = eng.services.get<Window>();
    if (!window_) return false;
    if (TTF_Init() != 0) { LOG_E("ui", "TTF_Init: %s (text disabled)", TTF_GetError()); }
    else {
        for (auto* p : kFontCandidates) {
            if (FILE* f = std::fopen(p, "rb")) { std::fclose(f); fontPath_ = p; break; }
        }
        if (fontPath_.empty()) LOG_W("ui", "no font found - text disabled");
    }
    loadTheme();
    eng.services.provide<UIHandler>(this);
    return true;
}

void UIHandler::shutdown(engine::Engine& eng) {
    eng.services.withdraw<UIHandler>();
    clearTextCache(); // GL context is still alive: window shuts down after us
    for (auto& [s, f] : fonts_) TTF_CloseFont(f);
    fonts_.clear();
    if (TTF_WasInit()) TTF_Quit();
}

bool UIHandler::loadTheme(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false; // optional file: built-in defaults are fine
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    engine::Json j = engine::Json::parse(ss.str(), &err);
    if (!j.isObject()) { LOG_E("ui", "%s: %s", path.c_str(), err.c_str()); return false; }
    auto col = [&](const char* key, Color& c) {
        const engine::Json& a = j[key];
        if (a.size() >= 3) c = {(float)a.at(0).num(), (float)a.at(1).num(), (float)a.at(2).num(), (float)a.at(3).num(1.0)};
    };
    col("glass_top", theme.glassTop);   col("glass_bottom", theme.glassBottom);
    col("sheen", theme.sheen);          col("border", theme.border);
    col("glint", theme.glint);          col("shadow", theme.shadow);
    col("text", theme.text);            col("text_dim", theme.textDim);
    col("accent", theme.accent);
    theme.radius = (float)j["radius"].num(theme.radius);
    return true;
}

void UIHandler::addPanel(const std::string& name, int order, Panel fn) {
    removePanel(name);
    panels_.push_back({name, order, std::move(fn)});
    std::stable_sort(panels_.begin(), panels_.end(),
                     [](const Entry& a, const Entry& b) { return a.order < b.order; });
}

void UIHandler::removePanel(const std::string& name) {
    panels_.erase(std::remove_if(panels_.begin(), panels_.end(),
                                 [&](const Entry& e) { return e.name == name; }), panels_.end());
}

// ---------------- frame ----------------
void UIHandler::onFrameBegin(engine::Engine&) {
    Uint32 b = SDL_GetMouseState(&mx_, &my_);
    bool down = (b & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    if (down && !mouseDown_) clicked_ = true;
    mouseDown_ = down;
    if (!mouseDown_) activeSlider_.clear();
}

void UIHandler::onFrameEnd(engine::Engine&) { clicked_ = false; }

void UIHandler::onRenderUI(engine::Engine&) {
    w_ = window_->width();
    h_ = window_->height();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, w_, h_, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    auto panels = panels_; // a panel may add/remove panels while we iterate
    for (auto& p : panels) p.fn(*this);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glDisable(GL_LINE_SMOOTH);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

// ---------------- primitives ----------------
void UIHandler::rect(float x, float y, float w, float h, float r, float g, float b, float a) {
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
}

// Points around a rounded rectangle, clockwise from the top-right corner.
static void outline(std::vector<std::pair<float, float>>& pts, float x, float y, float w, float h, float r) {
    r = std::max(0.0f, std::min(r, std::min(w, h) / 2));
    const int N = 8;
    const float cx[4] = {x + w - r, x + w - r, x + r, x + r};
    const float cy[4] = {y + r, y + h - r, y + h - r, y + r};
    for (int c = 0; c < 4; c++) {
        float start = (-90.0f + 90.0f * c) * (float)M_PI / 180.0f;
        for (int i = 0; i <= N; i++) {
            float a = start + (float)M_PI / 2 * i / N;
            pts.push_back({cx[c] + std::cos(a) * r, cy[c] + std::sin(a) * r});
        }
    }
}

void UIHandler::roundedRect(float x, float y, float w, float h, float radius, const Color& top, const Color& bottom) {
    std::vector<std::pair<float, float>> pts;
    outline(pts, x, y, w, h, radius);
    Color mid = mix(top, bottom, 0.5f);
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(mid.r, mid.g, mid.b, mid.a);
    glVertex2f(x + w / 2, y + h / 2);
    for (size_t i = 0; i <= pts.size(); i++) {
        auto& p = pts[i % pts.size()];
        Color c = mix(top, bottom, h > 0 ? (p.second - y) / h : 0);
        glColor4f(c.r, c.g, c.b, c.a);
        glVertex2f(p.first, p.second);
    }
    glEnd();
}

void UIHandler::glass(float x, float y, float w, float h, float alpha, bool focused, float radius) {
    float r = radius >= 0 ? radius : theme.radius;

    // soft shadow: a few growing, faint layers offset downward
    for (int i = 5; i >= 1; i--) {
        Color s = scaleAlpha(theme.shadow, alpha * 0.18f / i);
        roundedRect(x - i * 2, y - i * 2 + 6, w + i * 4, h + i * 4, r + i * 2, s, s);
    }
    // frosted body
    Color top = theme.glassTop, bot = theme.glassBottom;
    if (focused) { top = mix(top, theme.accent, 0.22f); bot = mix(bot, theme.accent, 0.12f); }
    roundedRect(x, y, w, h, r, scaleAlpha(top, alpha), scaleAlpha(bot, alpha));
    // sheen over the upper half, fading out
    Color sh = scaleAlpha(theme.sheen, alpha), sh0 = sh; sh0.a = 0;
    roundedRect(x + 1, y + 1, w - 2, h * 0.5f, std::max(0.0f, r - 1), sh, sh0);

    // border
    Color bc = focused ? theme.accent : theme.border;
    bc.a *= alpha;
    std::vector<std::pair<float, float>> pts;
    outline(pts, x, y, w, h, r);
    glLineWidth(focused ? 1.8f : 1.2f);
    glColor4f(bc.r, bc.g, bc.b, bc.a);
    glBegin(GL_LINE_LOOP);
    for (auto& p : pts) glVertex2f(p.first, p.second);
    glEnd();

    // glint along the top edge
    Color g = scaleAlpha(theme.glint, alpha);
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    glColor4f(g.r, g.g, g.b, 0); glVertex2f(x + r * 0.6f, y + 1.5f);
    glColor4f(g.r, g.g, g.b, g.a); glVertex2f(x + w * 0.5f, y + 1.5f);
    glColor4f(g.r, g.g, g.b, g.a); glVertex2f(x + w * 0.5f, y + 1.5f);
    glColor4f(g.r, g.g, g.b, 0); glVertex2f(x + w - r * 0.6f, y + 1.5f);
    glEnd();
}

TTF_Font* UIHandler::font(int size) {
    if (fontPath_.empty()) return nullptr;
    TTF_Font*& f = fonts_[size];
    if (!f) f = TTF_OpenFont(fontPath_.c_str(), size);
    return f;
}

int UIHandler::textWidth(const std::string& s, int size) {
    TTF_Font* f = font(size);
    int w = 0, h = 0;
    if (f && !s.empty()) TTF_SizeUTF8(f, s.c_str(), &w, &h);
    return w;
}

void UIHandler::textCentered(float cx, float y, const std::string& s, int size, const Color& c) {
    text(cx - textWidth(s, size) / 2.0f, y, s, size, c);
}

void UIHandler::clearTextCache() {
    for (auto& [k, t] : textCache_) glDeleteTextures(1, &t.id);
    textCache_.clear();
}

// Renders a string to a GL texture once; identical strings reuse it on later frames.
const UIHandler::TextTex* UIHandler::textTexture(const std::string& s, int size) {
    std::string key = std::to_string(size) + ":" + s;
    auto it = textCache_.find(key);
    if (it != textCache_.end()) return &it->second;

    TTF_Font* f = font(size);
    if (!f) return nullptr;
    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* raw = TTF_RenderUTF8_Blended(f, s.c_str(), white);
    if (!raw) return nullptr;
    SDL_Surface* surf = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ABGR8888, 0); // RGBA byte order
    SDL_FreeSurface(raw);
    if (!surf) return nullptr;

    if (textCache_.size() >= 512) clearTextCache(); // e.g. a live-changing number; keep memory bounded

    TextTex t{0, surf->w, surf->h};
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, surf->pitch / 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, surf->w, surf->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, surf->pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    SDL_FreeSurface(surf);
    return &(textCache_[key] = t);
}

void UIHandler::text(float x, float y, const std::string& s, int size, float r, float g, float b, float a) {
    if (s.empty()) return;
    const TextTex* t = textTexture(s, size);
    if (!t) return;

    glBindTexture(GL_TEXTURE_2D, t->id);
    glEnable(GL_TEXTURE_2D);
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(x, y);
    glTexCoord2f(1, 0); glVertex2f(x + t->w, y);
    glTexCoord2f(1, 1); glVertex2f(x + t->w, y + t->h);
    glTexCoord2f(0, 1); glVertex2f(x, y + t->h);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

// ---------------- pointer + widgets ----------------
bool UIHandler::pointerFree() const { return SDL_GetRelativeMouseMode() == SDL_FALSE; }

bool UIHandler::hovered(float x, float y, float w, float h) const {
    return pointerFree() && mx_ >= x && mx_ <= x + w && my_ >= y && my_ <= y + h;
}

static const int kLabelSize = 18;

bool UIHandler::button(const std::string& label, float x, float y, float w, float h, bool focused) {
    bool hot = hovered(x, y, w, h);
    bool held = hot && mouseDown_;
    glass(x, y, w, h, held ? 0.7f : (hot || focused ? 1.0f : 0.8f), focused || hot, 10);
    textCentered(x + w / 2, y + (h - kLabelSize * 1.3f) / 2, label, kLabelSize, focused || hot ? theme.text : theme.textDim);
    return hot && clicked_;
}

bool UIHandler::toggle(const std::string& label, float x, float y, float w, float h, bool value, bool focused) {
    bool hot = hovered(x, y, w, h);
    glass(x, y, w, h, hot || focused ? 1.0f : 0.8f, focused || hot, 10);
    text(x + 18, y + (h - kLabelSize * 1.3f) / 2, label, kLabelSize, focused || hot ? theme.text : theme.textDim);
    // pill switch on the right
    float pw = 44, ph = 22, px = x + w - pw - 18, py = y + (h - ph) / 2;
    Color off{0.3f, 0.36f, 0.45f, 0.5f}, on = scaleAlpha(theme.accent, 0.8f);
    Color track = value ? on : off;
    roundedRect(px, py, pw, ph, ph / 2, track, track);
    float kx = value ? px + pw - ph + 3 : px + 3;
    roundedRect(kx, py + 3, ph - 6, ph - 6, (ph - 6) / 2, {1, 1, 1, 0.95f}, {0.85f, 0.9f, 1, 0.95f});
    return (hot && clicked_) ? !value : value;
}

float UIHandler::slider(const std::string& label, float x, float y, float w, float h,
                        float value, float lo, float hi, bool focused) {
    bool hot = hovered(x, y, w, h);
    glass(x, y, w, h, hot || focused ? 1.0f : 0.8f, focused || hot, 10);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f", value);
    text(x + 18, y + 8, label, kLabelSize, focused || hot ? theme.text : theme.textDim);
    std::string v = buf;
    text(x + w - 18 - textWidth(v, kLabelSize), y + 8, v, kLabelSize, theme.accent);

    float tx = x + 18, tw = w - 36, ty = y + h - 16;
    Color track{0.3f, 0.36f, 0.45f, 0.5f};
    roundedRect(tx, ty - 2, tw, 4, 2, track, track);

    std::string id = label + "@" + std::to_string((int)y);
    if (hot && clicked_ && my_ >= ty - 12) activeSlider_ = id;
    if (activeSlider_ == id && mouseDown_ && pointerFree()) {
        float t = std::clamp((mx_ - tx) / tw, 0.0f, 1.0f);
        value = lo + t * (hi - lo);
    }
    float t = std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f);
    Color fill = scaleAlpha(theme.accent, 0.85f);
    roundedRect(tx, ty - 2, tw * t, 4, 2, fill, fill);
    roundedRect(tx + tw * t - 7, ty - 7, 14, 14, 7, {1, 1, 1, 0.95f}, {0.85f, 0.9f, 1, 0.95f});
    return value;
}

REGISTER_MODULE(UIHandler);

} // namespace core
