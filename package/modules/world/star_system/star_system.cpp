// world/star_system - one seeded sun with planets and moons on analytic orbits, drawn cheaply (placeholder low-poly spheres).
// Provides world::IStarSystem. Rules in star_system_rules.h; approach and tunables in docs/WORLD.md.
#include <GL/gl.h>
#include <algorithm>
#include <cmath>
#include "core/physics_world/physics_api.h"
#include "core/render_engine/render_engine.h"
#include "core/save_system/save_api.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "world/star_system/star_system_api.h"
#include "world/star_system/star_system_rules.h"

class StarSystemModule : public engine::Module, public world::IStarSystem, public core::ISaveable {
public:
    const char* name() const override { return "world/star_system"; }
    std::vector<std::string> dependencies() const override { return {"core/render_engine"}; }
    std::vector<std::string> optionalDependencies() const override { return {"core/save_system", "core/physics_world"}; }

    bool init(engine::Engine& eng) override {
        auto& c = eng.config;
        if (!c.get("star_system.enabled", true, "generate and draw the star system")) return true;
        params_.seed = (unsigned)c.get("world.seed", 1234.0f, "seed of the star system (same seed = same system)");
        params_.planets = c.get("planets.count", 6, "number of planets in the star system, 2-10");
        params_.sunDistance = c.get("world.sun_distance", 12000.0f, "distance of the sun from the spawn point, units");
        timeScale_ = c.get("world.time_scale", 1.0f, "speed of the planets' orbits (1 = real seconds; try 100 to watch them move)");
        int detail = std::clamp(c.get("world.sphere_detail", 1, "planet mesh detail: 0 = 12x8 segments, 1 = 16x12"), 0, 1);
        sphere_ = world::buildSphere(detail ? 16 : 12, detail ? 12 : 8);

        render_ = &eng.services.require<core::RenderEngine>();
        generate();
        if ((physics_ = eng.services.get<core::IPhysics>())) registerBodies();
        eng.services.provide<world::IStarSystem>(this);
        if ((saves_ = eng.services.get<core::ISaveSystem>())) saves_->registerSaveable(this);
        render_->addPass("star_system", 50, [this](core::RenderEngine& r) { draw(r); });   // after starfield/skybox, before the demo rocks (100)
        active_ = true;
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        if (!active_) return;
        render_->removePass("star_system");
        if (saves_) saves_->unregisterSaveable(this);
        unregisterBodies();
        eng.services.withdraw<world::IStarSystem>();
    }

    // sim time only advances in the fixed update, which does not run while the game is paused
    void onFixedUpdate(engine::Engine&, float dt) override {
        if (!active_) return;
        time_ += (double)dt * timeScale_;
        if (physics_) { prev_.resize(sys_.bodies.size()); for (size_t i = 0; i < prev_.size(); i++) prev_[i] = sys_.bodies[i].position; }
        world::updatePositions(sys_, time_);
        if (!physics_) return;
        // move every body's collision sphere along its orbit; the velocity makes the closing speed relative to the moving body
        // (the physics world steps after us, priority 10, and sweeps the motion since the last step)
        for (size_t i = 0; i < sys_.bodies.size(); i++) {
            auto v = world::finiteVelocity(prev_[i], sys_.bodies[i].position, (double)dt);
            physics_->setBody(bodyIds_[i], toF(sys_.bodies[i].position), {(float)v.x, (float)v.y, (float)v.z});
        }
    }

    // ---- world::IStarSystem ----
    unsigned seed() const override { return sys_.seed; }
    double simTime() const override { return time_; }
    world::Vec3d sunPosition() const override { return sys_.sun; }
    const std::vector<world::Body>& bodies() const override { return sys_.bodies; }
    world::Vec3d positionAt(int id) const override {
        return id >= 0 && id < (int)sys_.bodies.size() ? sys_.bodies[id].position : sys_.sun;
    }

    // ---- saving: the seed and the sim time are enough to rebuild every position ----
    const char* saveId() const override { return "world/star_system"; }
    engine::Json save() const override { return engine::Json::object().set("seed", (double)sys_.seed).set("time", time_); }
    void load(const engine::Json& j) override {
        unsigned seed = (unsigned)j["seed"].num((double)params_.seed);
        if (seed != params_.seed) { params_.seed = seed; generate(); }
        time_ = std::max(0.0, j["time"].num(time_));
        world::updatePositions(sys_, time_);
        if (physics_) for (size_t i = 0; i < sys_.bodies.size(); i++) physics_->teleport(bodyIds_[i], toF(sys_.bodies[i].position));   // a jump, not a sweep
    }

private:
    static engine::Vec3 toF(const world::Vec3d& p) { return {(float)p.x, (float)p.y, (float)p.z}; }

    // every body is a static sphere of the drawn radius, kind "sun" / "planet" / "moon"
    void registerBodies() {
        unregisterBodies();
        for (auto& b : sys_.bodies) bodyIds_.push_back(physics_->addBody(world::physicsKind(b.kind), toF(b.position), b.radius, false));
    }
    void unregisterBodies() {
        if (physics_) for (auto id : bodyIds_) physics_->removeBody(id);
        bodyIds_.clear();
    }

    void generate() {
        sys_ = world::generateSystem(params_);
        world::updatePositions(sys_, time_);
        if (physics_) registerBodies();
        order_.resize(sys_.bodies.size());
        proj_.resize(sys_.bodies.size());
        const auto& s = sys_.bodies[0];
        LOG_I("star_system", "seed %u: sun radius %.0f colour (%.2f %.2f %.2f) at (%.0f, %.0f, %.0f), %zu bodies", sys_.seed, s.radius, s.color[0], s.color[1], s.color[2], sys_.sun.x, sys_.sun.y, sys_.sun.z, sys_.bodies.size());
        for (auto& b : sys_.bodies)
            LOG_D("star_system", "%-18s r=%6.0f orbit=%8.0f period=%9.0f s parent=%d", b.name.c_str(), b.radius, b.orbitRadius, b.period, b.parent);
    }

    void drawBody(const world::Body& b, const world::Projected& p, const float* view) {
        glPushMatrix();
        glTranslatef(p.x, p.y, p.z);
        if (b.kind == world::BodyKind::Sun) {
            glDisable(GL_LIGHTING);
            glColor3fv(b.color);
        } else {   // light comes from the sun: direction from this body to it (w = 0: a directional light)
            float dx = (float)(sys_.sun.x - b.position.x), dy = (float)(sys_.sun.y - b.position.y), dz = (float)(sys_.sun.z - b.position.z);
            float l = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (l < 1e-3f) l = 1;
            const float pos[4] = {dx / l, dy / l, dz / l, 0.0f};
            glLightfv(GL_LIGHT0, GL_POSITION, pos);
            glEnable(GL_LIGHTING);
            glColor3fv(b.color);
        }
        glScalef(p.radius, p.radius, p.radius);
        glDrawElements(GL_TRIANGLES, (GLsizei)sphere_.indices.size(), GL_UNSIGNED_SHORT, sphere_.indices.data());
        glPopMatrix();
        if (b.kind == world::BodyKind::Sun) drawGlow(b, p, view);
    }

    // soft additive halo: a camera-facing fan, opaque-ish in the middle and transparent at the rim
    void drawGlow(const world::Body& b, const world::Projected& p, const float* view) {
        const float rx = view[0], ry = view[4], rz = view[8], ux = view[1], uy = view[5], uz = view[9];   // camera right / up in world axes
        const float R = p.radius * 2.6f;
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        glPushMatrix();
        glTranslatef(p.x, p.y, p.z);
        glBegin(GL_TRIANGLE_FAN);
        glColor4f(b.color[0], b.color[1], b.color[2], 0.8f);
        glVertex3f(0, 0, 0);
        glColor4f(b.color[0], b.color[1], b.color[2], 0.0f);
        for (int i = 0; i <= 24; i++) {
            float a = (float)i * (2.0f * 3.14159265f / 24.0f), cx = std::cos(a) * R, cy = std::sin(a) * R;
            glVertex3f(rx * cx + ux * cy, ry * cx + uy * cy, rz * cx + uz * cy);
        }
        glEnd();
        glPopMatrix();
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }

    void draw(core::RenderEngine& r) {
        if (sys_.bodies.empty()) return;
        float m[16];
        for (int i = 0; i < 16; i++) m[i] = r.camera.view[i];
        // camera position from the view matrix (rotation R, translation t: pos = -R^T t); rendering is camera-relative, so drop t
        world::Vec3d cam{-((double)m[0] * m[12] + (double)m[1] * m[13] + (double)m[2] * m[14]),
                         -((double)m[4] * m[12] + (double)m[5] * m[13] + (double)m[6] * m[14]),
                         -((double)m[8] * m[12] + (double)m[9] * m[13] + (double)m[10] * m[14])};
        m[12] = m[13] = m[14] = 0;
        glLoadMatrixf(m);
        const double clampDist = r.camera.farZ * 0.75;

        int n = (int)sys_.bodies.size();
        for (int i = 0; i < n; i++) { proj_[i] = world::projectBody(sys_.bodies[i].position, sys_.bodies[i].radius, cam, clampDist); order_[i] = i; }
        std::sort(order_.begin(), order_.end(), [&](int a, int b) { return proj_[a].dist > proj_[b].dist; });   // far to near

        glPushAttrib(GL_ENABLE_BIT | GL_LIGHTING_BIT | GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_POLYGON_BIT);
        glEnable(GL_LIGHT0);
        const float sc[3] = {sys_.bodies[0].color[0], sys_.bodies[0].color[1], sys_.bodies[0].color[2]};
        const float diffuse[4] = {sc[0], sc[1], sc[2], 1}, ambient[4] = {0, 0, 0, 1}, globalAmbient[4] = {0.06f, 0.06f, 0.08f, 1};
        glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
        glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, globalAmbient);
        glEnable(GL_COLOR_MATERIAL);
        glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
        glEnable(GL_NORMALIZE);
        glDisable(GL_TEXTURE_2D);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_NORMAL_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, sphere_.verts.data());
        glNormalPointer(GL_FLOAT, 0, sphere_.verts.data());   // unit sphere: the position is the normal

        // pass 1: bodies pulled in to the clamp distance are a backdrop, painted far to near without touching depth
        glDisable(GL_DEPTH_TEST);
        for (int id : order_) if (proj_[id].clamped) drawBody(sys_.bodies[id], proj_[id], m);
        // pass 2: bodies within reach are real geometry
        glEnable(GL_DEPTH_TEST);
        for (int id : order_) if (!proj_[id].clamped) drawBody(sys_.bodies[id], proj_[id], m);

        glDisableClientState(GL_NORMAL_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        glPopAttrib();
    }

    core::RenderEngine* render_ = nullptr;
    core::ISaveSystem* saves_ = nullptr;
    core::IPhysics* physics_ = nullptr;
    std::vector<core::BodyId> bodyIds_;
    std::vector<world::Vec3d> prev_;
    bool active_ = false;
    world::SystemParams params_;
    world::System sys_;
    world::SphereMesh sphere_;
    std::vector<int> order_;
    std::vector<world::Projected> proj_;
    double time_ = 0;
    float timeScale_ = 1.0f;
};

REGISTER_MODULE(StarSystemModule);
