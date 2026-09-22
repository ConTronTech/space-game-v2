// ui/toast - generic notification popups other modules opt into through core::IToast (toast_api.h). See docs/TOAST.md.
// Bottom-right corner (the one corner the HUD, orbit guide, debugger and profiler leave free), newest at the bottom,
// at most toast.max_visible on screen, the rest queued. State machine / fade / layout are in toast_rules.h (unit-tested).
// No per-frame allocation: a fixed ring of entries, each toast's text width measured once, text textures cached by core::UIHandler.
#include <algorithm>
#include <cmath>
#include <sstream>
#include "core/ui_handler/ui_handler.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "ui/toast/toast_api.h"
#include "ui/toast/toast_rules.h"

namespace ui {

class Toast : public engine::Module, public core::IToast {
public:
    const char* name() const override { return "ui/toast"; }
    std::vector<std::string> dependencies() const override { return {"core/ui_handler"}; }

    bool init(engine::Engine& eng) override {
        eng_ = &eng;
        if (!eng.config.get("toast.enabled", true, "generic notification popups (bottom-right) that other modules can show through core::IToast")) {
            LOG_I("toast", "off (toast.enabled = false)");
            return true;
        }
        enabled_ = true;
        stack_.setMaxVisible(eng.config.get("toast.max_visible", 4, "toasts on screen at once; more wait in a queue and appear as older ones expire"));
        seconds_ = std::clamp((float)eng.config.get("toast.seconds", 4.0, "default toast lifetime in seconds (the last 0.5 s is a fade-out)"), 0.5f, 60.0f);
        ui_ = eng.services.get<core::UIHandler>();
        eng.services.provide<core::IToast>(this);
        if (ui_) ui_->addPanel("ui/toast", 880, [this](core::UIHandler& ui) { draw(ui); });   // above the HUD, below the debugger (890) and menus (900+)

        // dev aid for screenshots: --toast-test (one Info toast) or --toast-test=info,warning:Low fuel,urgent  (level[:text], comma separated).
        // Test toasts last 600 s so a --screenshot frame catches them whatever the frame count.
        if (eng.hasFlag("toast-test") || !eng.flagValue("toast-test").empty()) {
            std::string spec = eng.flagValue("toast-test");
            if (spec.empty()) spec = "info";
            std::stringstream ss(spec);
            for (std::string item; std::getline(ss, item, ',');) {
                if (item.empty()) continue;
                auto colon = item.find(':');
                std::string lv = item.substr(0, colon), text = colon == std::string::npos ? "" : item.substr(colon + 1);
                Level l = lv == "urgent" ? Level::Urgent : lv == "warning" ? Level::Warning : Level::Info;
                if (text.empty()) text = l == Level::Urgent ? "URGENT: test toast" : l == Level::Warning ? "Warning: test toast" : "Info: test toast";
                show(text, l, 600.0f);
            }
        }
        LOG_I("toast", "on: max %d visible, %.1f s each%s", stack_.maxVisible(), seconds_, stack_.size() ? " (--toast-test)" : "");
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        if (!enabled_) return;
        if (ui_) ui_->removePanel("ui/toast");
        eng.services.withdraw<core::IToast>();
        stack_.clear();
        enabled_ = false;
    }

    // ---------- core::IToast ----------
    void show(const std::string& text, Level level, float seconds) override {
        if (!enabled_) return;
        stack_.add(level, text, seconds > 0 ? seconds : seconds_, eng_->time());
        if (stack_.dropped() != reported_) { reported_ = stack_.dropped(); LOG_W("toast", "queue full (%d): dropped the oldest waiting toast", toast::Stack::kCapacity); }
    }
    void clear() override { stack_.clear(); }

private:
    static core::Color col(const toast::RGB& c, float a) { return {c.r, c.g, c.b, a}; }

    void draw(core::UIHandler& ui) {
        double now = eng_->time();   // engine time keeps running while paused, so toasts still expire in the pause menu
        stack_.update(now);
        const int n = stack_.visible();
        if (n == 0) return;
        const float s = ui.scale;
        const int fs = std::max(10, (int)std::round(14 * s));
        const float h = std::round(30 * s), gap = std::round(6 * s), margin = std::round(16 * s);
        const float bottomClear = std::round(40 * s);   // keep clear of the bottom-centre controls hint row
        for (int i = 0; i < n; i++) {
            toast::Entry& e = stack_.at(i);
            float a = stack_.alpha(i, now);
            if (a <= 0) continue;
            if (e.width < 0) e.width = (float)ui.textWidth(e.text, fs);
            float w = std::max(160 * s, e.width + 34 * s);   // long text makes a wide toast: keep messages short
            float x = toast::slotX(w, (float)ui.width(), margin);
            float y = toast::slotY(i, n, (float)ui.height() - bottomClear, margin, h, gap);
            float k = a * toast::flash(e.level, now);
            toast::RGB c = toast::levelColor(e.level);
            ui.glass(x, y, w, h, a, false, 8 * s);
            ui.rect(x + 6 * s, y + 7 * s, 3 * s, h - 14 * s, c.r, c.g, c.b, k);                 // level stripe
            ui.text(x + 18 * s, y + (h - fs * 1.3f) / 2, e.text, fs, col(c, k));
        }
    }

    engine::Engine* eng_ = nullptr;
    core::UIHandler* ui_ = nullptr;
    bool enabled_ = false;
    float seconds_ = 4.0f;
    unsigned long reported_ = 0;
    toast::Stack stack_{4};
};

REGISTER_MODULE(Toast);

} // namespace ui
