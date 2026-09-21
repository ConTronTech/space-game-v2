#pragma once
// core/ui_handler - 2D overlay: ordered panels, a glass theme, and simple immediate-mode widgets.
// Pixel coordinates, origin top-left.
//
//     auto& ui = eng.services.require<core::UIHandler>();
//     ui.addPanel("my_hud", 10, [](core::UIHandler& ui){
//         ui.glass(10, 10, 220, 40);                       // frosted translucent panel
//         ui.text(24, 20, "Hello", 16, ui.theme.text);
//         if (ui.button("Click me", 10, 60, 220, 40, false)) { ... }
//     });
// Colors/radius come from config/ui/theme.json (optional) - see docs/UI.md.
#include <SDL2/SDL_ttf.h>
#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "core/ui_handler/text_cache.h"
#include "engine/module.h"

namespace core {

struct Color { float r = 1, g = 1, b = 1, a = 1; };

// Layout scale for any window size: 1.0 at 1280x720, follows the SMALLER of width/720-relative and height ratios so 5:4 (1280x1024), 4:3 (800x600, 640x480) and
// ultrawide windows never overflow; clamped to 0.6..3 (the HUD uses the same formula, ship_hud/hud_logic.h uiScale).
inline float uiLayoutScale(int w, int h) {
    float s = std::min(w / 1280.0f, h / 720.0f);
    return s < 0.6f ? 0.6f : (s > 3.0f ? 3.0f : s);
}

struct Theme {
    Color glassTop    {0.16f, 0.22f, 0.34f, 0.55f};   // fill, top of panel
    Color glassBottom {0.06f, 0.09f, 0.16f, 0.42f};   // fill, bottom of panel
    Color sheen       {1.00f, 1.00f, 1.00f, 0.07f};   // bright wash over the top half
    Color border      {0.75f, 0.88f, 1.00f, 0.28f};
    Color glint       {1.00f, 1.00f, 1.00f, 0.35f};   // thin highlight along the top edge
    Color shadow      {0.00f, 0.00f, 0.00f, 0.30f};
    Color text        {0.93f, 0.96f, 1.00f, 1.00f};
    Color textDim     {0.62f, 0.70f, 0.80f, 1.00f};
    Color accent      {0.35f, 0.85f, 1.00f, 1.00f};   // focus / active
    float radius = 12.0f;
};

class UIHandler : public engine::Module {
public:
    using Panel = std::function<void(UIHandler&)>;

    const char* name() const override { return "core/ui_handler"; }
    std::vector<std::string> dependencies() const override { return {"core/window", "core/render_engine"}; }
    int priority() const override { return -50; }
    bool init(engine::Engine&) override;
    void shutdown(engine::Engine&) override;
    void onFrameBegin(engine::Engine&) override;
    void onRenderUI(engine::Engine&) override;
    void onFrameEnd(engine::Engine&) override;

    void addPanel(const std::string& name, int order, Panel fn);
    void removePanel(const std::string& name);

    Theme theme;
    float scale = 1.0f;                       // uiLayoutScale of the current window, updated every frame: widgets and panels size themselves with it
    bool loadTheme(const std::string& path = "config/ui/theme.json");

    // ---- primitives ----
    void rect(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f);
    void text(float x, float y, const std::string& s, int size, float r, float g, float b, float a = 1.0f);
    void text(float x, float y, const std::string& s, int size, const Color& c) { text(x, y, s, size, c.r, c.g, c.b, c.a); }
    void textCentered(float cx, float y, const std::string& s, int size, const Color& c);
    int textWidth(const std::string& s, int size);
    void roundedRect(float x, float y, float w, float h, float radius, const Color& top, const Color& bottom);

    // ---- glass ----
    // Translucent frosted panel: soft shadow, gradient fill, sheen, border and a top-edge glint.
    // 'alpha' scales the whole thing; 'focused' tints it with the accent color.
    void glass(float x, float y, float w, float h, float alpha = 1.0f, bool focused = false, float radius = -1);

    // ---- HUD widgets (additive) ----
    // Glass-style horizontal bar: dark track + 'frac' (0..1) fill in 'fill' with a top sheen. Draws no text and never reacts to the pointer.
    void bar(float x, float y, float w, float h, float frac, const Color& fill);
    // Screen-edge tint that fades to transparent towards the middle ('thickness' in pixels; 'c.a' is the edge alpha). For damage/death.
    void vignette(const Color& c, float thickness);

    // ---- pointer + widgets (only react while the cursor is free, e.g. in a menu) ----
    bool pointerFree() const;
    int mouseX() const { return mx_; }
    int mouseY() const { return my_; }
    bool hovered(float x, float y, float w, float h) const;
    bool button(const std::string& label, float x, float y, float w, float h, bool focused);   // true on click
    bool toggle(const std::string& label, float x, float y, float w, float h, bool value, bool focused); // returns new value
    float slider(const std::string& label, float x, float y, float w, float h,
                 float value, float lo, float hi, bool focused, const char* fmt = "%.0f");       // returns new value

    int width() const { return w_; }
    int height() const { return h_; }

private:
    struct Entry { std::string name; int order; Panel fn; };
    TTF_Font* font(int size);
    struct TextTex { unsigned int id; int w, h; };
    const TextTex* textTexture(const std::string& s, int size);
    void clearTextCache();
    TextCache<TextTex> textCache_;             // (size, string) -> GL texture; stale entries are dropped when it grows large
    TextCache<int> widthCache_;                // (size, string) -> pixel width, so centred text does not re-measure every frame
    unsigned long frame_ = 0;                  // engine frame number, for the caches
    std::vector<Entry> panels_;
    std::map<int, TTF_Font*> fonts_;
    std::string fontPath_;
    class Window* window_ = nullptr;
    int w_ = 0, h_ = 0;
    int mx_ = 0, my_ = 0;
    bool mouseDown_ = false, clicked_ = false;
    std::string activeSlider_;
    struct InjectedClick { int x, y; long frame; };
    bool injectParsed_ = false;            // --ui-click dev aid (see onFrameBegin)
    std::vector<InjectedClick> injectClicks_;
};

} // namespace core
