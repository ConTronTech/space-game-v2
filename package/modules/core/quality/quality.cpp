// core/quality - picks a graphics preset (Low/Medium/High/Ultra; Auto by default) and applies it as a layer of DEFAULT values under
// engine.config, BEFORE the other modules read their tunables. Explicit game.json values always win. See docs/QUALITY.md.
// Order: settings (-1500) < window (-1000, creates the GL context whose strings we read) < quality (-900) < everything that reads
// quality-scaled tunables (import_handler -200, render_engine -100, world modules depend on render_engine).
#include <GL/gl.h>
#include <SDL2/SDL.h>
#include <cstdio>
#include "core/quality/quality_api.h"
#include "core/quality/quality_rules.h"
#include "core/settings/settings_api.h"
#include "engine/engine.h"
#include "engine/log.h"

namespace core {

class Quality : public engine::Module, public IQuality {
public:
    const char* name() const override { return "core/quality"; }
    std::vector<std::string> dependencies() const override { return {"core/window"}; }
    std::vector<std::string> optionalDependencies() const override { return {"core/settings"}; }
    int priority() const override { return -900; }

    bool init(engine::Engine& eng) override {
        // 1. hardware facts (the window already created the GL context)
        quality::Hardware hw;
        const char* ren = (const char*)glGetString(GL_RENDERER);
        const char* ven = (const char*)glGetString(GL_VENDOR);
        const char* ver = (const char*)glGetString(GL_VERSION);
        hw.renderer = ren ? ren : ""; hw.vendor = ven ? ven : ""; hw.version = ver ? ver : "";
        GLint maxTex = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
        hw.maxTexture = (int)maxTex;
        hw.cpuThreads = SDL_GetCPUCount();
        hw.ramMb = SDL_GetSystemRAM();
        SDL_DisplayMode dm;
        if (SDL_GetDesktopDisplayMode(0, &dm) == 0) { hw.displayW = dm.w; hw.displayH = dm.h; }
        detected_ = quality::detect(hw);

        // 2. which preset: --quality flag > settings (the pause menu) > game.json "quality.preset" > auto
        std::string source = "default";
        quality::Preset want = quality::Preset::Auto;
        std::string text = eng.flagValue("quality");
        if (!text.empty()) source = "--quality flag";
        if (text.empty()) if (auto* s = eng.services.get<ISettings>()) { text = s->get("quality.preset", std::string()); if (!text.empty()) source = "settings"; }
        if (text.empty()) { text = eng.config.get<std::string>("quality.preset", "auto", "graphics preset: auto | low | medium | high | ultra (the pause-menu setting wins over this)"); source = "game.json or default"; }
        auto parsed = quality::parsePreset(text);
        if (parsed.unknown) LOG_W("quality", "unknown preset '%s' (from %s): using auto", text.c_str(), source.c_str());
        want = parsed.preset;
        selected_ = want;
        active_ = want == quality::Preset::Auto ? detected_.preset : want;

        // 3. apply as the preset layer
        std::string applied;
        for (auto& e : quality::presetTable()) {
            double v = quality::valueFor(e, active_);
            eng.config.setPresetNumber(e.key, v);
            char b[96]; std::snprintf(b, sizeof b, "%s%s=%g", applied.empty() ? "" : ", ", e.key, v);
            applied += b;
        }

        LOG_I("quality", "hardware: %s | %s | max texture %d | %d CPU threads | %d MB RAM | display %dx%d", hw.renderer.c_str(), hw.vendor.c_str(),
              hw.maxTexture, hw.cpuThreads, hw.ramMb, hw.displayW, hw.displayH);
        LOG_I("quality", "auto would pick '%s' (%s)", quality::presetName(detected_.preset), detected_.reason.c_str());
        LOG_I("quality", "preset in use: %s%s (chosen by %s); game.json values still override it", quality::presetName(active_),
              want == quality::Preset::Auto ? " (auto)" : "", source.c_str());

        LOG_I("quality", "preset values: %s", applied.c_str());
        eng.services.provide<IQuality>(this);
        return true;
    }
    void shutdown(engine::Engine& eng) override {
        if (eng.services.get<IQuality>() == static_cast<IQuality*>(this)) eng.services.withdraw<IQuality>();
    }

    // Safety net: Auto only, never changes anything, just tells the player.
    void onUpdate(engine::Engine& eng, float dt) override {
        if (checked_ || selected_ != quality::Preset::Auto || eng.paused()) return;
        elapsed_ += dt; frames_++;
        if (elapsed_ < 10.0f) return;
        checked_ = true;
        float fps = frames_ / elapsed_;
        if (fps < 25.0f && active_ != quality::Preset::Low)
            LOG_W("quality", "average %.0f fps over the first %.0f s on the auto preset '%s': try a lower one in Settings > Graphics", fps, elapsed_, quality::presetName(active_));
    }

    std::string selectedName() const override { return quality::presetName(selected_); }
    std::string activeName() const override { return quality::presetName(active_); }
    std::string detectedName() const override { return quality::presetName(detected_.preset); }
    std::string reason() const override { return detected_.reason; }

private:
    quality::Detection detected_;
    quality::Preset selected_ = quality::Preset::Auto, active_ = quality::Preset::Medium;
    float elapsed_ = 0; int frames_ = 0; bool checked_ = false;
};

REGISTER_MODULE(Quality);

} // namespace core
