// fx/particles - a small, cheap particle system: engine exhaust, hit sparks, collision debris, a warp flash, and whatever other modules ask for
// through the fx::SpawnParticles event. Rules in particles_rules.h; API, presets file and budgets in docs/FX.md.
#include <GL/gl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include "core/camera/camera_api.h"
#include "core/data_registry/data_api.h"
#include "core/input_handler/input_api.h"
#include "core/physics_world/physics_api.h"
#include "core/render_engine/render_engine.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "fx/particles/particles_api.h"
#include "fx/particles/particles_rules.h"
#include "ship/ship_core/ship_api.h"
#include "ship/warp_drive/warp_api.h"

class Particles : public engine::Module {
public:
    const char* name() const override { return "fx/particles"; }
    std::vector<std::string> dependencies() const override { return {"core/render_engine"}; }
    std::vector<std::string> optionalDependencies() const override {
        return {"ship/ship_core", "ship/fake_ship", "core/physics_world", "ship/warp_drive", "world/star_system", "core/data_registry", "core/camera", "core/input_handler"};
    }

    bool init(engine::Engine& eng) override {
        auto& c = eng.config;
        if (!c.get("fx.enabled", true, "particle effects (exhaust, sparks, debris, warp flash)")) return true;
        int cap = std::clamp(c.get("fx.max_particles", 800, "most live particles at once; spawns beyond it are dropped"), 16, 20000);
        spawnBudget_ = std::max(1, c.get("fx.spawn_budget", 150, "most particles spawned per frame (a big burst is spread over frames by dropping the excess)"));
        sizeScale_ = std::max(0.0f, c.get("fx.size_scale", 1.0f, "multiplier on every particle's size"));
        nozzleBack_ = c.get("fx.nozzle_back", 7.9f, "exhaust origin: metres behind the pilot's eye (ShipV2's thrusters)");
        nozzleDown_ = c.get("fx.nozzle_down", 0.4f, "exhaust origin: metres below the pilot's eye");
        exhaust_ = std::clamp(c.get("fx.exhaust", 1.0f, "engine exhaust amount: 0 = off, 0.5 = half as many particles, 1 = full"), 0.0f, 2.0f);
        limits_.maxDist = c.get("fx.max_distance", 2500.0f, "particles farther than this are not drawn, units");
        soft_ = c.get("fx.soft_dots", true, "draw particles as soft round dots with one tiny 32x32 texture (false = plain hard squares, no texture)");
        limits_.nearCull = 1.5f;   // not a tunable: closer than this to the camera is the ship's own cockpit
        presets_ = fx::defaultPresets();
        if (auto* data = eng.services.get<core::IData>()) loadPresets(*data);
        pool_.init(cap);
        quads_.init(cap);
        if (soft_) {   // one tiny shared texture, made once
            std::vector<uint8_t> px;
            fx::softDotTexture(32, px);
            glGenTextures(1, &tex_);
            glBindTexture(GL_TEXTURE_2D, tex_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        rng_ = fx::Rng(0x5eed1234u);
        testFrame_ = frameFlag(eng, "fx-test");
        testLoop_ = eng.hasFlag("fx-test-loop");
        fakeThrust_ = eng.hasFlag("fx-test-thrust");   // dev aid: behave as if full forward thrust were applied (exhaust without a keyboard)
        eng_ = &eng;
        input_ = eng.services.get<core::IInput>();

        eng.events.subscribe<fx::SpawnParticles>([this](const fx::SpawnParticles& e) { onSpawn(e); });
        if (eng.services.get<core::IPhysics>()) eng.events.subscribe<core::Collided>([this](const core::Collided& e) { onCollided(e); });
        eng.events.subscribe<ship::DamageTaken>([this](const ship::DamageTaken& e) { onDamage(e); });

        render_ = &eng.services.require<core::RenderEngine>();
        render_->addPass("fx/particles", 70, [this](core::RenderEngine& r) { draw(r); });   // after the star system (50), asteroids (60) and stations (65); the cockpit is 800
        active_ = true;
        LOG_I("fx", "%zu presets, %d particles max, exhaust %.2f, spawn budget %d per frame", presets_.size(), cap, exhaust_, spawnBudget_);
        return true;
    }

    void shutdown(engine::Engine&) override {
        if (!active_) return;
        render_->removePass("fx/particles");
        if (tex_) { glDeleteTextures(1, &tex_); tex_ = 0; }
    }

    void onUpdate(engine::Engine& eng, float dt) override {
        if (!active_) return;
        if (eng.paused()) return;                                    // frozen while the menu is open
        auto t0 = std::chrono::steady_clock::now();
        fx::update(pool_, std::min(dt, 0.1f));
        updateMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        pool_.newFrame(spawnBudget_);
        sources(eng, dt);
        long f = (long)eng.frame();
        if (testFrame_ >= 0 && (f == testFrame_ || (testLoop_ && f > testFrame_ && (f - testFrame_) % 10 == 0))) testBurst(eng);
    }

private:
    static long frameFlag(engine::Engine& eng, const char* key) {
        std::string v = eng.flagValue(key);
        return v.empty() ? -1 : std::atol(v.c_str());
    }

    void loadPresets(core::IData& data) {
        for (auto& id : data.ids("particles")) {
            const engine::Json& j = data.get("particles", id);
            if (!j.isObject()) continue;
            fx::Preset p;
            if (const fx::Preset* d = fx::findPreset(presets_, id)) p = *d;   // fields missing from the file keep the built-in value
            p.name = id;
            p.countMin = (int)j["count_min"].num(p.countMin); p.countMax = (int)j["count_max"].num(p.countMax);
            p.speedMin = (float)j["speed_min"].num(p.speedMin); p.speedMax = (float)j["speed_max"].num(p.speedMax);
            p.lifeMin = (float)j["life_min"].num(p.lifeMin); p.lifeMax = (float)j["life_max"].num(p.lifeMax);
            p.sizeStart = (float)j["size_start"].num(p.sizeStart); p.sizeEnd = (float)j["size_end"].num(p.sizeEnd);
            p.drag = (float)j["drag"].num(p.drag);
            p.additive = j["additive"].boolean(p.additive);
            p.spreadDeg = (float)j["spread"].num(p.spreadDeg);
            for (int k = 0; k < 4; k++) {
                p.colorStart[k] = (float)j["color_start"].at(k).num(p.colorStart[k]);
                p.colorEnd[k] = (float)j["color_end"].at(k).num(p.colorEnd[k]);
            }
            fx::sanitize(p);
            bool replaced = false;
            for (auto& q : presets_) if (q.name == id) { q = p; replaced = true; }
            if (!replaced) presets_.push_back(p);
        }
    }

    void onSpawn(const fx::SpawnParticles& e) {
        const fx::Preset* p = fx::findPreset(presets_, e.kind);
        if (!p) { if (!warned_) { LOG_W("fx", "unknown particle kind '%s' ignored", e.kind.c_str()); warned_ = true; } return; }
        fx::Emit em;
        em.position = e.position; em.direction = e.direction; em.velocity = e.velocity; em.count = e.count;
        em.sizeMul = e.size > 0 ? e.size : 1.0f; em.lifeMul = e.lifetime > 0 ? e.lifetime : 1.0f;
        for (int k = 0; k < 3; k++) em.colour[k] = e.colour[k];
        em.sizeScale = sizeScale_;
        fx::spawn(pool_, *p, em, rng_);
    }

    void emit(const char* kind, const world::Vec3d& pos, const world::Vec3d& dir, const world::Vec3d& vel, int count) {
        fx::SpawnParticles e;
        e.kind = kind; e.position = pos; e.direction = dir; e.velocity = vel; e.count = count;
        eng_->events.emit(e);
    }

    void onCollided(const core::Collided& c) {
        if (!active_) return;
        engine::Vec3 n = c.normal;                                   // a -> b at the moment of contact
        world::Vec3d hit{c.posA.x + n.x * c.radiusA, c.posA.y + n.y * c.radiusA, c.posA.z + n.z * c.radiusA};
        int count = fx::impactCount(c.speed, 40);
        emit("spark", hit, {-n.x, -n.y, -n.z}, {}, count);
        emit("debris", hit, {-n.x, -n.y, -n.z}, {}, count / 3 + 1);
    }

    void onDamage(const ship::DamageTaken& d) {
        if (!active_) return;
        auto* ship = eng_->services.get<ship::IShip>();
        if (!ship) return;
        auto p = ship->position(); auto v = ship->velocity();
        emit("spark", {p.x, p.y, p.z}, {}, {v.x, v.y, v.z}, std::clamp((int)(d.amount / 3.0f), 2, 10));
    }

    // exhaust while thrusting (chase view only) and the warp flash
    void sources(engine::Engine& eng, float dt) {
        auto* ship = eng.services.get<ship::IShip>();
        auto* tf = eng.services.get<core::ITransformSource>();
        if (!ship || !tf) return;
        const auto& st = ship->status();
        if (auto* drive = eng.services.get<ship::IWarpDrive>()) {
            bool engaged = drive->engaged();
            if (engaged && !wasWarping_) {
                core::Pose ps = tf->transform(eng.alpha());
                emit("warp_flash", {ps.pos.x + ps.fwd.x * 8.0, ps.pos.y + ps.fwd.y * 8.0, ps.pos.z + ps.fwd.z * 8.0}, {}, {}, 0);
            }
            wasWarping_ = engaged;
        }
        auto* cam = eng.services.get<core::ICamera>();
        if (exhaust_ <= 0.0f || !input_ || !st.alive || (cam && !cam->showsShip())) { exhaustCarry_ = 0; return; }   // only visible in chase view
        core::Pose ps = tf->transform(eng.alpha());
        engine::Vec3 right = engine::normalize(engine::cross(ps.fwd, ps.up));
        float t = fakeThrust_ ? 1.0f : input_->value("thrust"), s = input_->value("strafe"), l = input_->value("lift");
        engine::Vec3 accel = ps.fwd * t + right * s + ps.up * l;
        float level = engine::length(accel);
        int n = fx::emitCount(level, 60.0f * exhaust_, dt, exhaustCarry_);
        if (n <= 0) return;
        engine::Vec3 back = accel * (-1.0f / level);                 // the exhaust goes opposite to the push
        auto v = ship->velocity();
        // the nozzle: at the rear for forward thrust, else at the ship's centre
        // (ShipV2: the thruster jets sit ~7.9 m behind the pilot's eye and ~0.4 m below it; both are tunables)
        float rear = t > 0.05f ? nozzleBack_ : 0.0f, drop = t > 0.05f ? nozzleDown_ : 0.0f;
        world::Vec3d origin{ps.pos.x - ps.fwd.x * rear - ps.up.x * drop, ps.pos.y - ps.fwd.y * rear - ps.up.y * drop, ps.pos.z - ps.fwd.z * rear - ps.up.z * drop};
        emit("exhaust", origin, {back.x, back.y, back.z}, {v.x, v.y, v.z}, n);
    }

    // --fx-test=FRAME: a row of every effect 15 units in front of the ship (screenshots; --fx-test-loop repeats it for cost measurements)
    void testBurst(engine::Engine& eng) {
        auto* tf = eng.services.get<core::ITransformSource>();
        if (!tf) return;
        core::Pose ps = tf->transform(eng.alpha());
        engine::Vec3 right = engine::normalize(engine::cross(ps.fwd, ps.up));
        const char* kinds[4] = {"spark", "debris", "warp_flash", "exhaust"};
        for (int k = 0; k < 4; k++) {
            float side = (k - 1.5f) * 6.0f;
            world::Vec3d at{ps.pos.x + ps.fwd.x * 15.0 + right.x * side, ps.pos.y + ps.fwd.y * 15.0 + right.y * side, ps.pos.z + ps.fwd.z * 15.0 + right.z * side};
            emit(kinds[k], at, {ps.up.x, ps.up.y, ps.up.z}, {}, k == 2 ? 60 : 40);
        }
    }

    void draw(core::RenderEngine& r) {
        if (pool_.n == 0) return;
        auto t0 = std::chrono::steady_clock::now();
        float m[16];
        for (int i = 0; i < 16; i++) m[i] = r.camera.view[i];
        fx::Camera cam;
        cam.pos = {-((double)m[0] * m[12] + (double)m[1] * m[13] + (double)m[2] * m[14]),
                   -((double)m[4] * m[12] + (double)m[5] * m[13] + (double)m[6] * m[14]),
                   -((double)m[8] * m[12] + (double)m[9] * m[13] + (double)m[10] * m[14])};
        cam.right[0] = m[0]; cam.right[1] = m[4]; cam.right[2] = m[8];
        cam.up[0] = m[1]; cam.up[1] = m[5]; cam.up[2] = m[9];
        cam.fwd[0] = -m[2]; cam.fwd[1] = -m[6]; cam.fwd[2] = -m[10];
        m[12] = m[13] = m[14] = 0;
        glLoadMatrixf(m);
        fx::buildQuads(pool_, cam, limits_, quads_);
        if (quads_.alphaVerts + quads_.addVerts > 0) {
            glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT);
            glDisable(GL_LIGHTING); glDisable(GL_CULL_FACE);
            if (tex_) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex_); glEnableClientState(GL_TEXTURE_COORD_ARRAY); glTexCoordPointer(2, GL_FLOAT, 0, quads_.uv.data()); }
            else glDisable(GL_TEXTURE_2D);
            glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
            glVertexPointer(3, GL_FLOAT, 0, quads_.pos.data());
            glColorPointer(4, GL_FLOAT, 0, quads_.col.data());
            if (quads_.alphaVerts > 0) { glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDrawArrays(GL_QUADS, 0, quads_.alphaVerts); }
            if (quads_.addVerts > 0) { glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDrawArrays(GL_QUADS, quads_.alphaVerts, quads_.addVerts); }
            glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_VERTEX_ARRAY);
            if (tex_) { glDisableClientState(GL_TEXTURE_COORD_ARRAY); glBindTexture(GL_TEXTURE_2D, 0); }
            glPopAttrib();
        }
        drawMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        frames_++;
        if (eng_ && eng_->time() - lastReport_ >= 2.0) {
            lastReport_ = eng_->time();
            LOG_D("fx", "%d live, %d quads drawn, %ld dropped, CPU per frame: update %.3f ms, build+draw %.3f ms", pool_.n, (quads_.alphaVerts + quads_.addVerts) / 4, pool_.dropped,
                  frames_ ? updateMs_ / frames_ : 0.0, frames_ ? drawMs_ / frames_ : 0.0);
            updateMs_ = drawMs_ = 0; frames_ = 0;
        }
    }

    engine::Engine* eng_ = nullptr;
    core::RenderEngine* render_ = nullptr;
    core::IInput* input_ = nullptr;
    unsigned tex_ = 0;
    bool soft_ = true;
    bool active_ = false, warned_ = false, wasWarping_ = false, testLoop_ = false, fakeThrust_ = false;
    int spawnBudget_ = 150;
    float sizeScale_ = 1.0f, exhaust_ = 1.0f, exhaustCarry_ = 0, nozzleBack_ = 7.9f, nozzleDown_ = 0.4f;
    long testFrame_ = -1;
    fx::QuadLimits limits_;
    std::vector<fx::Preset> presets_;
    fx::Pool pool_;
    fx::QuadBuffers quads_;
    fx::Rng rng_;
    double updateMs_ = 0, drawMs_ = 0, lastReport_ = -10;
    long frames_ = 0;
};

REGISTER_MODULE(Particles);
