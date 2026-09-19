// gameplay/flight - Newtonian flight + starfield + speed readout.
// Doubles as the reference for how a game module uses the core services.
#include <GL/gl.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "core/input_handler/input_handler.h"
#include "core/render_engine/render_engine.h"
#include "core/ui_handler/ui_handler.h"
#include "engine/engine.h"
#include "engine/math.h"

using engine::Vec3;

class Flight : public engine::Module {
public:
    const char* name() const override { return "gameplay/flight"; }
    std::vector<std::string> dependencies() const override {
        return {"core/input_handler", "core/render_engine", "core/ui_handler"};
    }

    bool init(engine::Engine& eng) override {
        input_ = &eng.services.require<core::InputHandler>();
        render_ = &eng.services.require<core::RenderEngine>();
        auto& ui = eng.services.require<core::UIHandler>();

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

    void shutdown(engine::Engine&) override {
        render_->removePass("flight/stars");
        render_->removePass("flight/rocks");
    }

    void onFixedUpdate(engine::Engine&, float dt) override {
        // Controls don't snap: each axis eases toward what the player is asking for, so engines
        // spool up/down and the ship keeps turning for a moment after you let go.
        // Time constants: bigger = heavier ship, longer delay before it responds/stops.
        const float thrust = 40.0f, turn = 1.4f;
        const float turnTau = 0.25f;    // seconds to reach ~63% of a new turn rate
        const float engineTau = 0.30f;  // seconds for thrust to spool toward a new setting

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
        if (input_->down("brake")) vel_ *= std::max(0.0f, 1.0f - 2.0f * dt); // no drag otherwise: Newtonian
        pos_ += vel_ * dt;
    }

    void onUpdate(engine::Engine&, float) override {
        // publish the camera for the render engine
        float* m = render_->camera.view;
        Vec3 r = right_, u = up_, f = fwd_, p = pos_;
        m[0] = r.x; m[4] = r.y; m[8]  = r.z; m[12] = -engine::dot(r, p);
        m[1] = u.x; m[5] = u.y; m[9]  = u.z; m[13] = -engine::dot(u, p);
        m[2] = -f.x; m[6] = -f.y; m[10] = -f.z; m[14] = engine::dot(f, p);
        m[3] = 0; m[7] = 0; m[11] = 0; m[15] = 1;
    }

private:
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

    core::InputHandler* input_ = nullptr;
    core::RenderEngine* render_ = nullptr;
    // frame-rate independent exponential easing of 'cur' toward 'target'
    static float ease(float cur, float target, float tau, float dt) {
        return cur + (target - cur) * (1.0f - std::exp(-dt / tau));
    }

    float pitchRate_ = 0, yawRate_ = 0, rollRate_ = 0;       // smoothed turn inputs
    float thrustOut_ = 0, strafeOut_ = 0, liftOut_ = 0;      // smoothed engine output
    Vec3 pos_{0, 0, 0}, vel_{0, 0, 0};
    Vec3 fwd_{0, 0, -1}, up_{0, 1, 0}, right_{1, 0, 0};
    std::vector<Vec3> stars_;
    std::vector<Rock> rocks_;
};

REGISTER_MODULE(Flight);
