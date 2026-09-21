// world/starfield - stars on a far sphere that follow the camera (no parallax), deterministic. While the ship is warping each star
// becomes a short streak pointing away from the direction of travel. Vertex arrays, no per-frame allocation. See docs/WORLD.md.
#include <GL/gl.h>
#include <cmath>
#include <random>
#include <vector>
#include "core/render_engine/render_engine.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "ship/ship_core/ship_api.h"
#include "world/starfield/starfield_rules.h"

class Starfield : public engine::Module {
public:
    const char* name() const override { return "world/starfield"; }
    std::vector<std::string> dependencies() const override { return {"core/render_engine"}; }
    std::vector<std::string> optionalDependencies() const override { return {"ship/ship_core", "ship/fake_ship"}; }

    bool init(engine::Engine& eng) override {
        auto& c = eng.config;
        if (!c.get("starfield.enabled", true, "draw the star field")) return true;
        int count = std::clamp((int)c.get("starfield.count", 2500.0f, "number of stars"), 0, 100000);
        unsigned seed = (unsigned)c.get("starfield.seed", 1234.0f, "random seed for the star positions");
        pointSize_ = c.get("starfield.point_size", 1.5f, "star size in pixels");
        brightness_ = c.get("starfield.brightness", 1.0f, "star brightness multiplier (1 = normal)");
        streak_ = c.get("starfield.warp_streak_length", 300.0f, "length of warp streaks at full warp speed, world units (0 = no streaks)");
        refSpeed_ = c.get("starfield.warp_ref_speed", 2000.0f, "speed (m/s) at which streaks reach full length");

        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        for (int i = 0; i < count; i++) {
            float x, y, z, l;
            do { x = u(rng); y = u(rng); z = u(rng); l = std::sqrt(x * x + y * y + z * z); } while (l > 1.0f || l < 0.1f);
            dirs_.push_back(x / l); dirs_.push_back(y / l); dirs_.push_back(z / l);
        }
        points_.resize(dirs_.size());
        for (size_t i = 0; i < dirs_.size(); i++) points_[i] = dirs_[i] * kRadius;
        lines_.resize(dirs_.size() * 2);
        colors_.resize(dirs_.size() * 2);
        float b = std::clamp(brightness_, 0.0f, 4.0f);
        for (size_t s = 0; s < dirs_.size() / 3; s++) {          // head bright, tail dim
            const float head[3] = {0.9f * b, 0.9f * b, 1.0f * b}, tail[3] = {0.1f * b, 0.1f * b, 0.15f * b};
            for (int k = 0; k < 3; k++) { colors_[s * 6 + k] = head[k]; colors_[s * 6 + 3 + k] = tail[k]; }
        }
        eng_ = &eng;
        render_ = &eng.services.require<core::RenderEngine>();
        render_->addPass("starfield", 0, [this](core::RenderEngine& r) { draw(r); });
        active_ = true;
        LOG_I("starfield", "%zu stars (seed %u)", dirs_.size() / 3, seed);
        return true;
    }

    void shutdown(engine::Engine&) override {
        if (active_) render_->removePass("starfield");
    }

private:
    static constexpr float kRadius = 5000.0f;

    void draw(core::RenderEngine& r) {
        if (dirs_.empty()) return;
        float m[16];
        for (int i = 0; i < 16; i++) m[i] = r.camera.view[i];
        m[12] = m[13] = m[14] = 0;                                 // follow the camera: infinitely far
        glLoadMatrixf(m);

        float len = 0, vx = 0, vy = 0, vz = 0;
        if (auto* ship = eng_->services.get<ship::IShip>()) {
            const auto& st = ship->status();
            len = world::warpStreakLength(st.warping, st.speed, refSpeed_, streak_);
            auto v = ship->velocity();
            float sp = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            if (sp < 1e-3f) len = 0; else { vx = -v.x / sp; vy = -v.y / sp; vz = -v.z / sp; }   // trails point away from travel
        }

        glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_POINT_BIT | GL_LINE_BIT);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        glEnableClientState(GL_VERTEX_ARRAY);
        if (len > 0.0f) {
            for (size_t s = 0; s < points_.size() / 3; s++) {
                const float* p = &points_[s * 3];
                float* o = &lines_[s * 6];
                o[0] = p[0]; o[1] = p[1]; o[2] = p[2];
                o[3] = p[0] + vx * len; o[4] = p[1] + vy * len; o[5] = p[2] + vz * len;
            }
            glLineWidth(1.0f);
            glEnableClientState(GL_COLOR_ARRAY);
            glColorPointer(3, GL_FLOAT, 0, colors_.data());
            glVertexPointer(3, GL_FLOAT, 0, lines_.data());
            glDrawArrays(GL_LINES, 0, (GLsizei)(lines_.size() / 3));
            glDisableClientState(GL_COLOR_ARRAY);
        } else {
            float b = std::clamp(brightness_, 0.0f, 4.0f);
            glPointSize(pointSize_);
            glColor3f(0.9f * b, 0.9f * b, 1.0f * b);
            glVertexPointer(3, GL_FLOAT, 0, points_.data());
            glDrawArrays(GL_POINTS, 0, (GLsizei)(points_.size() / 3));
        }
        glDisableClientState(GL_VERTEX_ARRAY);
        glPopAttrib();
    }

    engine::Engine* eng_ = nullptr;
    core::RenderEngine* render_ = nullptr;
    bool active_ = false;
    float pointSize_ = 1.5f, brightness_ = 1.0f, streak_ = 300.0f, refSpeed_ = 2000.0f;
    std::vector<float> dirs_, points_, lines_, colors_;
};

REGISTER_MODULE(Starfield);
