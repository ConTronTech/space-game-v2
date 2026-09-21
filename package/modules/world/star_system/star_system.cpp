// world/star_system - one seeded sun with planets and moons on analytic orbits. Planets and moons are LOD terrain meshes (planet_mesh.h),
// built lazily one per frame; the sun and far dots use a low-poly sphere.
// Provides world::IStarSystem. Rules in star_system_rules.h; approach and tunables in docs/WORLD.md.
#include <GL/gl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include "core/physics_world/physics_api.h"
#include "core/render_engine/render_engine.h"
#include "core/save_system/save_api.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "world/star_system/planet_mesh.h"
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
        meshes_ = c.get("planet_mesh.enabled", true, "terrain meshes with level of detail for planets and moons (false = plain spheres)");
        terrainHeight_ = c.get("world.terrain_height", 0.03f, "planet terrain relief as a fraction of the radius (peak displacement)");
        maxLod_ = std::clamp(c.get("world.planet_max_lod", 3, "highest planet mesh level 0-4 (level L has 20*4^L triangles; 3 = 1,280, 4 = 5,120)"), 0, world::kMaxMeshLevel);
        triBudget_ = std::max(100, c.get("world.planet_triangle_budget", 20000, "most planet triangles drawn per frame"));
        edgePx_ = std::max(2.0f, c.get("world.planet_lod_edge_px", 12.0f, "target on-screen size of a mesh edge in pixels (smaller = more detail)"));
        cacheSeconds_ = std::max(1.0f, c.get("world.planet_mesh_cache_seconds", 30.0f, "free a planet mesh level after it was unused this long"));
        eng_ = &eng;

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
        cache_.clear();
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
        lod_.assign(sys_.bodies.size(), -1);
        meshFor_.assign(sys_.bodies.size(), nullptr);
        px_.assign(sys_.bodies.size(), 0.0f);
        cache_.clear();
        meshBytes_ = 0;
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
        const world::PlanetMesh* mesh = b.kind == world::BodyKind::Sun ? nullptr : meshFor_[b.id];
        if (mesh) {   // terrain mesh: interleaved position / normal / colour
            const char* base = (const char*)mesh->verts.data();
            glVertexPointer(3, GL_FLOAT, world::PlanetMesh::kStride, base);
            glNormalPointer(GL_FLOAT, world::PlanetMesh::kStride, base + 3 * sizeof(float));
            glColorPointer(3, GL_FLOAT, world::PlanetMesh::kStride, base + 6 * sizeof(float));
            glEnableClientState(GL_COLOR_ARRAY);
            glDrawElements(GL_TRIANGLES, (GLsizei)mesh->indices.size(), GL_UNSIGNED_SHORT, mesh->indices.data());
            glDisableClientState(GL_COLOR_ARRAY);
            glVertexPointer(3, GL_FLOAT, 0, sphere_.verts.data());
            glNormalPointer(GL_FLOAT, 0, sphere_.verts.data());
            trisDrawn_ += mesh->triangleCount();
        } else {   // the sun, or a body too small on screen for a mesh: a plain sphere (a very coarse one for dots)
            const world::SphereMesh& sph = (meshes_ && b.kind != world::BodyKind::Sun) ? dot_ : sphere_;
            if (&sph != &sphere_) { glVertexPointer(3, GL_FLOAT, 0, sph.verts.data()); glNormalPointer(GL_FLOAT, 0, sph.verts.data()); }
            glDrawElements(GL_TRIANGLES, (GLsizei)sph.indices.size(), GL_UNSIGNED_SHORT, sph.indices.data());
            if (&sph != &sphere_) { glVertexPointer(3, GL_FLOAT, 0, sphere_.verts.data()); glNormalPointer(GL_FLOAT, 0, sphere_.verts.data()); }
            trisDrawn_ += (int)sph.indices.size() / 3;
        }
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
        trisDrawn_ = 0;
        planMeshes(r);

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
        sweepCache();
        reportStats();
    }

    // ---- planet mesh LOD ----
    static uint64_t nowMicros() { return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

    static int cacheKey(int id, int level) { return id * 8 + level; }

    // Decide the level of every visible planet/moon (with hysteresis and the triangle budget), build at most ONE missing mesh, and pick what to draw now:
    // the wanted level if it exists, else the nearest level already built, else the plain sphere.
    void planMeshes(core::RenderEngine& r) {
        int n = (int)sys_.bodies.size();
        for (int i = 0; i < n; i++) meshFor_[i] = nullptr;
        if (!meshes_) return;
        GLint vp[4] = {0, 0, 1280, 720};
        glGetIntegerv(GL_VIEWPORT, vp);
        std::vector<int>& ids = planIds_;      // reused buffers: no per-frame allocation once warm
        ids.clear(); levels_.clear(); pxs_.clear();
        for (int i = 0; i < n; i++) {
            auto& b = sys_.bodies[i];
            if (b.kind == world::BodyKind::Sun) continue;
            px_[i] = world::pixelRadius(b.radius, proj_[i].dist, (float)vp[3], r.camera.fovDeg);
            if (px_[i] < 2.5f) { lod_[i] = -1; continue; }             // a dot: the sphere is plenty
            lod_[i] = world::chooseLod(world::wantedLevel(px_[i], edgePx_), lod_[i], maxLod_);
            ids.push_back(i); levels_.push_back(lod_[i]); pxs_.push_back(px_[i]);
        }
        world::applyTriangleBudget(levels_, pxs_, triBudget_);
        // build one missing mesh, the most important (biggest on screen) first
        int want = -1; float bestPx = 0;
        for (size_t k = 0; k < ids.size(); k++)
            if (!cache_.count(cacheKey(ids[k], levels_[k])) && pxs_[k] > bestPx) { bestPx = pxs_[k]; want = (int)k; }
        if (want >= 0) build(ids[want], levels_[want]);
        double now = eng_ ? eng_->time() : 0.0;
        for (size_t k = 0; k < ids.size(); k++) {
            for (int d = 0; d <= world::kMaxMeshLevel && !meshFor_[ids[k]]; d++)      // nearest built level to the wanted one
                for (int sgn : {-1, 1}) {
                    int lv = levels_[k] + sgn * d;
                    if (lv < 0 || lv > world::kMaxMeshLevel) continue;
                    auto it = cache_.find(cacheKey(ids[k], lv));
                    if (it != cache_.end()) { it->second.lastUsed = now; meshFor_[ids[k]] = &it->second.mesh; break; }
                }
        }
    }

    void build(int id, int level) {
        const auto& b = sys_.bodies[id];
        world::PlanetParams pp;
        pp.seed = world::mixSeed(params_.seed, (uint32_t)id);
        pp.terrainHeight = terrainHeight_;
        pp.moon = b.kind == world::BodyKind::Moon;
        for (int k = 0; k < 3; k++) pp.color[k] = b.color[k];
        uint64_t t0 = nowMicros();
        Entry e;
        e.mesh = world::buildPlanetMesh(level, pp);
        e.lastUsed = eng_ ? eng_->time() : 0.0;
        double ms = (double)(nowMicros() - t0) / 1000.0;
        meshBytes_ += e.mesh.bytes();
        builds_++;
        worstBuildMs_ = std::max(worstBuildMs_, ms);
        cache_[cacheKey(id, level)] = std::move(e);
        LOG_D("planet_mesh", "built %s level %d (%d vertices, %d triangles) in %.2f ms; %zu meshes, %.0f KB in memory", b.name.c_str(), level,
              world::meshVertexCount(level), world::meshTriangleCount(level), ms, cache_.size(), (double)meshBytes_ / 1024.0);
    }

    // free mesh levels that have not been drawn for a while
    void sweepCache() {
        if (!eng_) return;
        double now = eng_->time();
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (now - it->second.lastUsed > cacheSeconds_) { meshBytes_ -= it->second.mesh.bytes(); it = cache_.erase(it); }
            else ++it;
        }
    }

    void reportStats() {
        if (!eng_ || eng_->time() - lastReport_ < 2.0) return;
        lastReport_ = eng_->time();
        int drawn = 0;
        for (auto* m : meshFor_) drawn += m != nullptr;
        LOG_D("planet_mesh", "%d meshes drawn, %d triangles this frame, %zu meshes cached (%.0f KB), %d builds so far, slowest build %.2f ms", drawn, trisDrawn_,
              cache_.size(), (double)meshBytes_ / 1024.0, builds_, worstBuildMs_);
    }

    struct Entry { world::PlanetMesh mesh; double lastUsed = 0; };
    engine::Engine* eng_ = nullptr;
    bool meshes_ = true;
    float terrainHeight_ = 0.03f, edgePx_ = 12.0f, cacheSeconds_ = 30.0f;
    int maxLod_ = 3, triBudget_ = 20000, trisDrawn_ = 0, builds_ = 0;
    double lastReport_ = -10, worstBuildMs_ = 0;
    size_t meshBytes_ = 0;
    std::unordered_map<int, Entry> cache_;
    std::vector<int> lod_, planIds_, levels_;
    std::vector<float> px_, pxs_;
    std::vector<const world::PlanetMesh*> meshFor_;

    core::RenderEngine* render_ = nullptr;
    core::ISaveSystem* saves_ = nullptr;
    core::IPhysics* physics_ = nullptr;
    std::vector<core::BodyId> bodyIds_;
    std::vector<world::Vec3d> prev_;
    bool active_ = false;
    world::SystemParams params_;
    world::System sys_;
    world::SphereMesh sphere_, dot_ = world::buildSphere(8, 6);
    std::vector<int> order_;
    std::vector<world::Projected> proj_;
    double time_ = 0;
    float timeScale_ = 1.0f;
};

REGISTER_MODULE(StarSystemModule);
