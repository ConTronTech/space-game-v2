// gameplay/flight - Newtonian flight + starfield + speed readout.
// Doubles as the reference for how a game module uses the core services.
#include <GL/gl.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "core/audio/audio_api.h"
#include "core/input_handler/input_api.h"
#include "core/render_engine/render_engine.h"
#include "core/save_system/save_api.h"
#include "core/ui_handler/ui_handler.h"
#include "engine/engine.h"
#include "engine/math.h"

using engine::Vec3;

class Flight : public engine::Module, public core::ISaveable {
public:
    const char* name() const override { return "gameplay/flight"; }
    std::vector<std::string> dependencies() const override {
        return {"core/input_handler", "core/render_engine", "core/ui_handler"};
    }

    bool init(engine::Engine& eng) override {
        input_ = &eng.services.require<core::IInput>();
        render_ = &eng.services.require<core::RenderEngine>();
        auto& ui = eng.services.require<core::UIHandler>();

        // tunables: base values live here, config/game.json can override them (see docs/CONFIG.md)
        auto& c = eng.config;
        thrust_      = c.get("flight.thrust", 40.0f, "forward acceleration at full throttle, m/s^2");
        turnRate_    = c.get("flight.turn_rate", 1.4f, "max turn rate, rad/s");
        turnTau_     = c.get("flight.turn_tau", 0.25f, "seconds to reach ~63% of a new turn rate (higher = heavier ship)");
        engineTau_   = c.get("flight.engine_tau", 0.30f, "seconds for thrust to spool up/down (higher = laggier engines)");
        drift_       = std::clamp(c.get("flight.drift", 1.0f, "1.0 = pure Newtonian, nothing slows you. 0.0 = strong flight assist"), 0.0f, 1.0f);
        assist_      = c.get("flight.assist_strength", 4.0f, "damping per second when drift is 0 (scales with 1 - drift)");
        brake_       = c.get("flight.brake", 2.0f, "speed decay per second while braking");
        maxSpeed_    = c.get("flight.max_speed", 0.0f, "speed cap in m/s, 0 = unlimited");

        if ((saves_ = eng.services.get<core::ISaveSystem>())) saves_->registerSaveable(this);
        if ((audio_ = eng.services.get<core::IAudio>())) hum_ = audio_->playLoop("engine_loop", 0.0f, core::Bus::Engine);

        // 1. input: nothing to bind here. Actions (thrust, strafe, lift, pitch, yaw, roll, brake)
        //    are mapped to devices in config/input/<profile>.json

        // 2. register what we draw
        render_->addPass("flight/stars", 0, [this](core::RenderEngine& r) { drawStars(r); });
        render_->addPass("flight/rocks", 100, [this](core::RenderEngine&) { drawRocks(); });

        // 3. register our HUD
        ui.addPanel("flight/hud", 10, [this](core::UIHandler& ui) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.1f m/s", engine::length(vel_));
            ui.glass(16, 16, 200, 58);
            ui.text(32, 22, "SPEED", 12, ui.theme.textDim);
            ui.text(32, 38, buf, 22, ui.theme.accent);

            const char* hint = "W/S thrust   A/D strafe   Space/C up/down   mouse look   Q/E roll   X brake   Tab free mouse   Esc pause";
            float w = (float)ui.textWidth(hint, 13) + 40;
            ui.glass((ui.width() - w) / 2, ui.height() - 52.0f, w, 34, 0.9f, false, 10);
            ui.textCentered(ui.width() / 2.0f, ui.height() - 44.0f, hint, 13, ui.theme.textDim);
        });

        // world content
        std::srand(1234);
        for (int i = 0; i < 2500; i++) stars_.push_back(randomDir());
        for (int i = 0; i < 150; i++) {
            rocks_.push_back({randomDir() * (60.0f + rnd() * 800.0f), 2.0f + rnd() * 14.0f});
        }
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        render_->removePass("flight/stars");
        render_->removePass("flight/rocks");
        if (auto* ui = eng.services.get<core::UIHandler>()) ui->removePanel("flight/hud");
        if (saves_) saves_->unregisterSaveable(this);
        if (audio_ && hum_) audio_->stopLoop(hum_);
    }

    void onFixedUpdate(engine::Engine&, float dt) override {
        // Controls don't snap: each axis eases toward what the player is asking for, so engines
        // spool up/down and the ship keeps turning for a moment after you let go.
        prevPos_ = pos_; prevFwd_ = fwd_; prevUp_ = up_; // for render interpolation
        const float thrust = thrust_, turn = turnRate_, turnTau = turnTau_, engineTau = engineTau_;

        pitchRate_  = ease(pitchRate_,  input_->value("pitch"),  turnTau, dt);
        yawRate_    = ease(yawRate_,    input_->value("yaw"),    turnTau, dt);
        rollRate_   = ease(rollRate_,   input_->value("roll"),   turnTau, dt);
        thrustOut_  = ease(thrustOut_,  input_->value("thrust"), engineTau, dt);
        strafeOut_  = ease(strafeOut_,  input_->value("strafe"), engineTau, dt);
        liftOut_    = ease(liftOut_,    input_->value("lift"),   engineTau, dt);

        // orientation: rotate the basis around its own axes
        float pitch = pitchRate_ * turn * dt;
        float yaw   = yawRate_ * turn * dt;
        float roll  = rollRate_ * turn * dt;
        fwd_ = engine::rotate(fwd_, right_, pitch);  up_ = engine::rotate(up_, right_, pitch);
        fwd_ = engine::rotate(fwd_, up_, yaw);       right_ = engine::rotate(right_, up_, yaw);
        right_ = engine::rotate(right_, fwd_, roll); up_ = engine::rotate(up_, fwd_, roll);
        fwd_ = engine::normalize(fwd_);
        right_ = engine::normalize(engine::cross(fwd_, up_));
        up_ = engine::cross(right_, fwd_);

        Vec3 acc = fwd_ * thrustOut_ + right_ * strafeOut_ + up_ * liftOut_;
        vel_ += acc * (thrust * dt);
        if (input_->down("brake")) vel_ *= std::exp(-brake_ * dt);
        if (drift_ < 1.0f) vel_ *= std::exp(-(1.0f - drift_) * assist_ * dt); // flight assist; drift 1.0 = none
        if (maxSpeed_ > 0.0f) {
            float sp = engine::length(vel_);
            if (sp > maxSpeed_) vel_ *= maxSpeed_ / sp;
        }
        pos_ += vel_ * dt;
    }

    void onUpdate(engine::Engine& eng, float) override {
        // publish the camera for the render engine
        float* m = render_->camera.view;
        if (audio_ && hum_) {   // idle rumble, louder with throttle; silent while the menu is open
            float throttle = std::min(1.0f, std::abs(thrustOut_) + 0.6f * std::abs(strafeOut_) + 0.6f * std::abs(liftOut_));
            audio_->setLoopVolume(hum_, eng.paused() ? 0.0f : 0.25f + 0.75f * throttle);
        }
        // blend previous -> current physics state so 144 Hz displays don't show 60 Hz steps
        float a = eng.alpha();
        Vec3 p = engine::lerp(prevPos_, pos_, a);
        Vec3 f = engine::normalize(engine::lerp(prevFwd_, fwd_, a));
        Vec3 u = engine::normalize(engine::lerp(prevUp_, up_, a));
        Vec3 r = engine::normalize(engine::cross(f, u));
        u = engine::cross(r, f);
        m[0] = r.x; m[4] = r.y; m[8]  = r.z; m[12] = -engine::dot(r, p);
        m[1] = u.x; m[5] = u.y; m[9]  = u.z; m[13] = -engine::dot(u, p);
        m[2] = -f.x; m[6] = -f.y; m[10] = -f.z; m[14] = engine::dot(f, p);
        m[3] = 0; m[7] = 0; m[11] = 0; m[15] = 1;
    }

    // ---- saving: position, velocity and orientation. Control easing is not saved (it settles in a fraction of a second).
    const char* saveId() const override { return "gameplay/flight"; }
    engine::Json save() const override {
        return engine::Json::object().set("pos", vec(pos_)).set("vel", vec(vel_)).set("fwd", vec(fwd_)).set("up", vec(up_));
    }
    void load(const engine::Json& j) override {
        pos_ = readVec(j["pos"], pos_);
        vel_ = readVec(j["vel"], vel_);
        Vec3 f = engine::normalize(readVec(j["fwd"], fwd_)), u = engine::normalize(readVec(j["up"], up_));
        if (engine::length(f) < 0.5f || engine::length(u) < 0.5f || std::abs(engine::dot(f, u)) > 0.99f) { f = {0, 0, -1}; u = {0, 1, 0}; } // bad data
        fwd_ = f;
        right_ = engine::normalize(engine::cross(fwd_, u));
        up_ = engine::cross(right_, fwd_);
        pitchRate_ = yawRate_ = rollRate_ = thrustOut_ = strafeOut_ = liftOut_ = 0;
        prevPos_ = pos_; prevFwd_ = fwd_; prevUp_ = up_;   // no interpolation smear across the jump
    }

private:
    static engine::Json vec(const Vec3& v) { return engine::Json::array().push(v.x).push(v.y).push(v.z); }
    static Vec3 readVec(const engine::Json& j, Vec3 def) {
        if (j.size() < 3) return def;
        return {(float)j.at(0).num(def.x), (float)j.at(1).num(def.y), (float)j.at(2).num(def.z)};
    }

    struct Rock { Vec3 pos; float size; };

    static float rnd() { return std::rand() / (float)RAND_MAX; }
    static Vec3 randomDir() {
        Vec3 v;
        do { v = {rnd() * 2 - 1, rnd() * 2 - 1, rnd() * 2 - 1}; } while (engine::length(v) > 1.0f || engine::length(v) < 0.1f);
        return engine::normalize(v);
    }

    void drawStars(core::RenderEngine& r) {
        float m[16];
        for (int i = 0; i < 16; i++) m[i] = r.camera.view[i];
        m[12] = m[13] = m[14] = 0; // stars follow the camera: infinitely far
        glLoadMatrixf(m);
        glDisable(GL_DEPTH_TEST);
        glPointSize(1.5f);
        glColor3f(0.9f, 0.9f, 1.0f);
        glBegin(GL_POINTS);
        for (auto& s : stars_) glVertex3f(s.x * 5000, s.y * 5000, s.z * 5000);
        glEnd();
        glEnable(GL_DEPTH_TEST);
    }

    void drawRocks() {
        glColor3f(0.55f, 0.5f, 0.45f);
        for (auto& r : rocks_) {
            glPushMatrix();
            glTranslatef(r.pos.x, r.pos.y, r.pos.z);
            float s = r.size;
            const float v[6][3] = {{s,0,0},{-s,0,0},{0,s,0},{0,-s,0},{0,0,s},{0,0,-s}};
            const int e[12][2] = {{0,2},{0,3},{0,4},{0,5},{1,2},{1,3},{1,4},{1,5},{2,4},{4,3},{3,5},{5,2}};
            glBegin(GL_LINES);
            for (auto& ed : e) { glVertex3fv(v[ed[0]]); glVertex3fv(v[ed[1]]); }
            glEnd();
            glPopMatrix();
        }
    }

    core::IInput* input_ = nullptr;
    core::RenderEngine* render_ = nullptr;
    core::ISaveSystem* saves_ = nullptr;
    core::IAudio* audio_ = nullptr;
    int hum_ = 0;
    // frame-rate independent exponential easing of 'cur' toward 'target'
    static float ease(float cur, float target, float tau, float dt) {
        return cur + (target - cur) * (1.0f - std::exp(-dt / tau));
    }

    float thrust_ = 40, turnRate_ = 1.4f, turnTau_ = 0.25f, engineTau_ = 0.3f;
    float drift_ = 1, assist_ = 4, brake_ = 2, maxSpeed_ = 0;

    float pitchRate_ = 0, yawRate_ = 0, rollRate_ = 0;       // smoothed turn inputs
    float thrustOut_ = 0, strafeOut_ = 0, liftOut_ = 0;      // smoothed engine output
    Vec3 pos_{0, 0, 0}, vel_{0, 0, 0};
    Vec3 fwd_{0, 0, -1}, up_{0, 1, 0}, right_{1, 0, 0};
    Vec3 prevPos_{0, 0, 0}, prevFwd_{0, 0, -1}, prevUp_{0, 1, 0};
    std::vector<Vec3> stars_;
    std::vector<Rock> rocks_;
};

REGISTER_MODULE(Flight);
