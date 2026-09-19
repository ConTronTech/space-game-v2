// core/input_methods/mouse - "device": "mouse"
//   { "device": "mouse", "axis": "x", "scale": -0.0005 }        // axis: "x" | "y" | "wheel"
//   { "device": "mouse", "button": "left" }                       // button: left|middle|right|x1|x2
// Axes behave like a stick: value = (pixels per second) * scale * sensitivity, so turn rate does
// not depend on frame rate. Movement only counts while the mouse is captured.
// Profile settings:  "devices": { "mouse": { "capture": true, "sensitivity": 1.0 } }
// Bind an action called "mouse_capture_toggle" to release/recapture the cursor.
#include <SDL2/SDL.h>
#include <algorithm>
#include "core/input_handler/input_api.h"
#include "core/input_handler/input_method.h"
#include "core/settings/settings_api.h"
#include "core/window/window.h"
#include "engine/engine.h"

class MouseMethod : public engine::Module, public core::InputMethod {
public:
    const char* name() const override { return "core/input_methods/mouse"; }
    std::vector<std::string> dependencies() const override { return {"core/input_handler"}; }
    bool init(engine::Engine& eng) override {
        eng_ = &eng;
        in_ = &eng.services.require<core::IInput>();
        in_->registerMethod(this);
        eng.events.subscribe<core::SdlEvent>([this](const core::SdlEvent& ev) {
            if (ev.e.type == SDL_MOUSEWHEEL) wheel_ += (float)ev.e.wheel.y;
        });
        auto readSens = [this, &eng] {
            if (auto* s = eng.services.get<core::ISettings>()) userSens_ = s->get("input.mouse_sensitivity", 1.0f);
        };
        readSens();
        eng.events.subscribe<core::SettingChanged>([readSens](const core::SettingChanged& e) {
            if (e.key == "input.mouse_sensitivity") readSens();
        });
        eng.events.subscribe<engine::PauseChanged>([this](const engine::PauseChanged& e) {
            if (e.paused) { capturedBeforePause_ = wantCapture_; wantCapture_ = false; }
            else wantCapture_ = capturedBeforePause_;
            applyCapture_ = true;
        });
        return true;
    }
    void shutdown(engine::Engine&) override {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        in_->unregisterMethod(this);
    }

    const char* device() const override { return "mouse"; }
    void clearBindings() override { bindings_.clear(); }
    void configure(const engine::Json& s) override {
        bool capture = s["capture"].boolean(false);
        sensitivity_ = (float)s["sensitivity"].num(1.0);
        // If the game is paused right now (menu open) keep the cursor free; capture is restored on resume.
        capturedBeforePause_ = capture;
        wantCapture_ = capture && !(eng_ && eng_->paused());
        applyCapture_ = true;
    }
    bool addBinding(const std::string& action, const engine::Json& b, std::string& err) override {
        Binding out{action, Kind::Axis, 0, (float)b["scale"].num(1.0)};
        if (b.has("axis")) {
            std::string a = b["axis"].str();
            if (a == "x") out.code = 0; else if (a == "y") out.code = 1; else if (a == "wheel") out.code = 2;
            else { err = "unknown mouse axis '" + a + "' (x, y, wheel)"; return false; }
        } else if (b.has("button")) {
            std::string n = b["button"].str();
            out.kind = Kind::Button;
            if (n == "left") out.code = SDL_BUTTON_LEFT; else if (n == "middle") out.code = SDL_BUTTON_MIDDLE;
            else if (n == "right") out.code = SDL_BUTTON_RIGHT; else if (n == "x1") out.code = SDL_BUTTON_X1;
            else if (n == "x2") out.code = SDL_BUTTON_X2;
            else { err = "unknown mouse button '" + n + "'"; return false; }
        } else { err = "mouse binding needs \"axis\" or \"button\""; return false; }
        bindings_.push_back(out);
        return true;
    }

    void poll(core::IInput& in) override {
        double now = eng_->time();
        float dt = std::max((float)(now - lastTime_), 1.0f / 240.0f);
        lastTime_ = now;

        if (applyCapture_) {
            SDL_SetRelativeMouseMode(wantCapture_ ? SDL_TRUE : SDL_FALSE);
            applyCapture_ = false;
            skipNext_ = true; // the first delta after a mode change is garbage
        }
        int dx = 0, dy = 0;
        Uint32 buttons = SDL_GetRelativeMouseState(&dx, &dy);
        if (skipNext_) { dx = dy = 0; skipNext_ = false; }
        float wheel = wheel_;
        wheel_ = 0;
        bool captured = SDL_GetRelativeMouseMode() == SDL_TRUE;

        for (auto& b : bindings_) {
            if (b.kind == Kind::Button) {
                if (buttons & SDL_BUTTON(b.code)) in.contribute(b.action, b.scale);
            } else if (captured) {
                float v = b.code == 0 ? dx / dt : b.code == 1 ? dy / dt : wheel;
                in.contribute(b.action, v * b.scale * sensitivity_ * userSens_);
            }
        }
    }

    void onUpdate(engine::Engine&, float) override {
        if (!eng_->paused() && in_->pressed("mouse_capture_toggle")) { wantCapture_ = !wantCapture_; applyCapture_ = true; }
    }

private:
    enum class Kind { Axis, Button };
    struct Binding { std::string action; Kind kind; int code; float scale; };
    std::vector<Binding> bindings_;
    engine::Engine* eng_ = nullptr;
    core::IInput* in_ = nullptr;
    bool wantCapture_ = false, applyCapture_ = false, skipNext_ = false, capturedBeforePause_ = false;
    float sensitivity_ = 1.0f, userSens_ = 1.0f, wheel_ = 0.0f;
    double lastTime_ = 0;
};

REGISTER_MODULE(MouseMethod);
