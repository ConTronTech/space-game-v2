// world/stations - a few space stations (orbital and planetary), drawn as a placeholder cube + cylinder, solid for the physics world.
// Provides world::IStations. Rules in stations_rules.h; behaviour and tunables in docs/STATIONS.md.
#include <GL/gl.h>
#include <algorithm>
#include <cmath>
#include "core/physics_world/physics_api.h"
#include "core/render_engine/render_engine.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "world/star_system/star_system_api.h"
#include "world/star_system/star_system_rules.h"   // projectBody: the camera-relative, far-clamped position
#include "world/stations/stations_api.h"
#include "world/stations/stations_rules.h"

namespace {
struct Part { std::vector<float> verts; std::vector<unsigned short> idx; };   // interleaved position + normal

Part buildCube() {   // unit cube (half extent 1), flat normals
    Part p;
    const float n[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; f++) {
        const float* nn = n[f];
        int a = (f / 2 + 1) % 3, b = (f / 2 + 2) % 3;                       // the two axes in the face plane
        for (int k = 0; k < 4; k++) {
            float v[3] = {nn[0], nn[1], nn[2]};
            v[a] = (k & 1) ? 1.0f : -1.0f; v[b] = (k & 2) ? 1.0f : -1.0f;
            p.verts.insert(p.verts.end(), {v[0], v[1], v[2], nn[0], nn[1], nn[2]});
        }
        unsigned short base = (unsigned short)(f * 4);
        p.idx.insert(p.idx.end(), {base, (unsigned short)(base + 1), (unsigned short)(base + 3), base, (unsigned short)(base + 3), (unsigned short)(base + 2)});
    }
    return p;
}

Part buildCylinder(int seg) {   // unit radius, half length 1, axis Y, with caps
    Part p;
    for (int i = 0; i <= seg; i++) {
        float a = 2.0f * 3.14159265f * i / seg, c = std::cos(a), s = std::sin(a);
        p.verts.insert(p.verts.end(), {c, -1.0f, s, c, 0.0f, s});
        p.verts.insert(p.verts.end(), {c, 1.0f, s, c, 0.0f, s});
    }
    for (int i = 0; i < seg; i++) {
        unsigned short a = (unsigned short)(i * 2);
        p.idx.insert(p.idx.end(), {a, (unsigned short)(a + 1), (unsigned short)(a + 2), (unsigned short)(a + 2), (unsigned short)(a + 1), (unsigned short)(a + 3)});
    }
    for (int cap = 0; cap < 2; cap++) {                                        // caps: a fan around a centre vertex
        float y = cap ? 1.0f : -1.0f;
        unsigned short centre = (unsigned short)(p.verts.size() / 6);
        p.verts.insert(p.verts.end(), {0.0f, y, 0.0f, 0.0f, y, 0.0f});
        unsigned short first = (unsigned short)(p.verts.size() / 6);
        for (int i = 0; i < seg; i++) {
            float a = 2.0f * 3.14159265f * i / seg;
            p.verts.insert(p.verts.end(), {std::cos(a), y, std::sin(a), 0.0f, y, 0.0f});
        }
        for (int i = 0; i < seg; i++)
            p.idx.insert(p.idx.end(), {centre, (unsigned short)(first + i), (unsigned short)(first + (i + 1) % seg)});
    }
    return p;
}
} // namespace

class Stations : public engine::Module, public world::IStations {
public:
    const char* name() const override { return "world/stations"; }
    std::vector<std::string> dependencies() const override { return {"core/render_engine", "world/star_system"}; }
    std::vector<std::string> optionalDependencies() const override { return {"core/physics_world"}; }

    bool init(engine::Engine& eng) override {
        auto& c = eng.config;
        if (!c.get("stations.enabled", true, "generate and draw space stations")) return true;
        unsigned seed = (unsigned)c.get("world.seed", 1234.0f, "seed of the star system (same seed = same system)");
        int count = c.get("stations.count", 2, "number of stations, 1-3");
        seed += (unsigned)c.get("stations.seed_offset", 0, "added to world.seed for the stations only (re-rolls where they are without changing the planets)");
        drawDist_ = c.get("stations.draw_distance", 15000.0f, "stations farther than this are drawn as a dot, units");
        sys_ = eng.services.get<world::IStarSystem>();
        if (!sys_) { LOG_W("stations", "no star system: no stations"); return true; }
        std::vector<world::ParentInfo> parents;
        for (auto& b : sys_->bodies()) {
            if (b.kind != world::BodyKind::Planet) continue;
            world::ParentInfo p; p.bodyId = b.id; p.radius = b.radius; p.name = b.name;
            for (auto& m : sys_->bodies()) if (m.parent == b.id) p.moonClearance = std::max(p.moonClearance, m.orbitRadius + m.radius * 2.0);
            parents.push_back(p);
        }
        stations_ = world::generateStations(seed, std::clamp(count, 1, 3), parents);
        cube_ = buildCube(); cyl_ = buildCylinder(16);
        infos_.resize(stations_.size());
        bodyIds_.assign(stations_.size(), core::kNoBody);
        physics_ = eng.services.get<core::IPhysics>();
        for (size_t i = 0; i < stations_.size(); i++) {
            update(i, 0.0, true);
            if (physics_) bodyIds_[i] = physics_->addBody("station", toF(infos_[i].position), stations_[i].half * 1.6f, false);
            LOG_I("stations", "%s: dock zone %.0f units at (%.0f, %.0f, %.0f)", infos_[i].name.c_str(), infos_[i].radius, infos_[i].position.x, infos_[i].position.y, infos_[i].position.z);
        }
        render_ = &eng.services.require<core::RenderEngine>();
        render_->addPass("stations", 65, [this](core::RenderEngine& r) { draw(r); });   // after the asteroids (60)
        eng.services.provide<world::IStations>(this);
        active_ = true;
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        if (!active_) return;
        render_->removePass("stations");
        eng.services.withdraw<world::IStations>();
        if (physics_) for (auto id : bodyIds_) if (id != core::kNoBody) physics_->removeBody(id);
    }

    // stations follow their planet (which moves) and, when orbital, circle it: recomputed analytically from the star system's time every step
    void onFixedUpdate(engine::Engine&, float dt) override {
        if (!active_) return;
        for (size_t i = 0; i < stations_.size(); i++) update(i, dt, false);
    }

    // ---- world::IStations ----
    int count() const override { return (int)stations_.size(); }
    world::StationInfo info(int i) const override { return i >= 0 && i < (int)infos_.size() ? infos_[i] : world::StationInfo{}; }
    int nearest(const world::Vec3d& p) const override {
        int best = -1; double bd = 1e300;
        for (size_t i = 0; i < infos_.size(); i++) {
            double d = world::sdetail::length(world::sdetail::sub(infos_[i].position, p));
            if (d < bd) { bd = d; best = (int)i; }
        }
        return best;
    }

private:
    static engine::Vec3 toF(const world::Vec3d& p) { return {(float)p.x, (float)p.y, (float)p.z}; }

    void update(size_t i, float dt, bool first) {
        const world::Station& s = stations_[i];
        world::StationInfo& in = infos_[i];
        world::Vec3d prev = in.position;
        double t = sys_->simTime();
        world::Vec3d parent = sys_->positionAt(s.parent), off = world::stationOffset(s, t);
        in.name = s.name; in.kind = s.kind; in.radius = s.dockRadius; in.scale = s.scale; in.half = s.half; in.parent = s.parent;
        in.position = world::sdetail::add(parent, off);
        in.up = world::stationUp(s);
        world::Vec3d v = (first || dt <= 0) ? world::Vec3d{} : world::sdetail::mul(world::sdetail::sub(in.position, prev), 1.0 / dt);
        bool jump = world::sdetail::length(v) > 5000.0;                       // a loaded game moved simulation time: a jump, not a sweep
        if (jump) v = {};
        in.velocity = v;
        if (physics_ && bodyIds_[i] != core::kNoBody) {
            if (first || jump) physics_->teleport(bodyIds_[i], toF(in.position));
            else physics_->setBody(bodyIds_[i], toF(in.position), toF(v));
        }
    }

    void drawMesh(const Part& p) {
        glVertexPointer(3, GL_FLOAT, 24, p.verts.data());
        glNormalPointer(GL_FLOAT, 24, (const char*)p.verts.data() + 12);
        glDrawElements(GL_TRIANGLES, (GLsizei)p.idx.size(), GL_UNSIGNED_SHORT, p.idx.data());
    }

    void part(const Part& p, float sx, float sy, float sz, float ty, const float* col) {
        glColor3fv(col);
        glPushMatrix();
        glTranslatef(0, ty, 0);
        glScalef(sx, sy, sz);
        drawMesh(p);
        glPopMatrix();
    }

    void drawStation(size_t i, const world::Vec3d& cam, double clampDist) {
        const world::Station& s = stations_[i];
        const world::StationInfo& in = infos_[i];
        world::Projected pr = world::projectBody(in.position, in.half, cam, clampDist);
        if (pr.dist > drawDist_) { farPts_.push_back(pr.x); farPts_.push_back(pr.y); farPts_.push_back(pr.z); return; }
        // orientation: local +Y = up (spin axis / surface normal)
        world::Vec3d up = in.up;
        world::Vec3d ref = std::fabs(up.z) < 0.9 ? world::Vec3d{0, 0, 1} : world::Vec3d{1, 0, 0};
        world::Vec3d r{up.y * ref.z - up.z * ref.y, up.z * ref.x - up.x * ref.z, up.x * ref.y - up.y * ref.x};
        r = world::sdetail::normalized(r);
        world::Vec3d f{r.y * up.z - r.z * up.y, r.z * up.x - r.x * up.z, r.x * up.y - r.y * up.x};
        float m[16] = {(float)r.x, (float)r.y, (float)r.z, 0, (float)up.x, (float)up.y, (float)up.z, 0, (float)f.x, (float)f.y, (float)f.z, 0, pr.x, pr.y, pr.z, 1};
        glPushMatrix();
        glMultMatrixf(m);
        float h = s.half;
        if (s.kind == world::StationKind::Orbital) {
            static const float cubeCol[3] = {0.75f, 0.78f, 0.86f}, cylCol[3] = {0.95f, 0.72f, 0.2f};
            part(cube_, h, h, h, 0, cubeCol);
            glRotatef((float)(world::spinAngle(s, sys_->simTime()) * 180.0 / 3.14159265), 0, 1, 0);   // the hub spins slowly about the axis
            part(cyl_, h * 0.3f, h * 1.6f, h * 0.3f, 0, cylCol);
            glRotatef(90, 1, 0, 0);
            part(cyl_, h * 0.3f, h * 1.6f, h * 0.3f, 0, cylCol);                                        // a second arm across: reads as a hub
        } else {
            static const float cubeCol[3] = {0.85f, 0.82f, 0.75f}, pillarCol[3] = {0.55f, 0.6f, 0.68f};
            part(cube_, h, h, h, 0, cubeCol);
            part(cyl_, h * 0.28f, h * 1.3f, h * 0.28f, -h * 2.3f, pillarCol);                           // the pillar down to the ground
        }
        glPopMatrix();
        drawn_++;
    }

    void draw(core::RenderEngine& r) {
        if (stations_.empty()) return;
        float m[16];
        for (int i = 0; i < 16; i++) m[i] = r.camera.view[i];
        world::Vec3d cam{-((double)m[0] * m[12] + (double)m[1] * m[13] + (double)m[2] * m[14]),
                         -((double)m[4] * m[12] + (double)m[5] * m[13] + (double)m[6] * m[14]),
                         -((double)m[8] * m[12] + (double)m[9] * m[13] + (double)m[10] * m[14])};
        m[12] = m[13] = m[14] = 0;
        glLoadMatrixf(m);
        glPushAttrib(GL_ENABLE_BIT | GL_LIGHTING_BIT | GL_CURRENT_BIT | GL_POINT_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT);
        setLight(cam);
        glEnable(GL_LIGHTING); glEnable(GL_LIGHT0);
        glEnable(GL_COLOR_MATERIAL); glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
        glEnable(GL_NORMALIZE);
        glDisable(GL_TEXTURE_2D);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_NORMAL_ARRAY);
        farPts_.clear(); drawn_ = 0;
        for (size_t i = 0; i < stations_.size(); i++) drawStation(i, cam, r.camera.farZ * 0.75);
        glDisableClientState(GL_NORMAL_ARRAY);
        if (!farPts_.empty()) {   // far stations: a small bright dot
            glDisable(GL_LIGHTING);
            glPointSize(4.0f);
            glColor3f(0.9f, 0.85f, 0.5f);
            glVertexPointer(3, GL_FLOAT, 0, farPts_.data());
            glDrawArrays(GL_POINTS, 0, (GLsizei)(farPts_.size() / 3));
        }
        glDisableClientState(GL_VERTEX_ARRAY);
        glPopAttrib();
    }

    // light from the sun, as the star system and the asteroids do; a fixed light without a star system
    void setLight(const world::Vec3d& cam) {
        float dir[4] = {0.5f, 0.8f, 0.3f, 0.0f}, diffuse[4] = {1, 1, 1, 1};
        if (sys_) {
            world::Vec3d s = sys_->sunPosition();
            double dx = s.x - cam.x, dy = s.y - cam.y, dz = s.z - cam.z, l = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (l > 1e-3) { dir[0] = (float)(dx / l); dir[1] = (float)(dy / l); dir[2] = (float)(dz / l); }
            if (!sys_->bodies().empty()) for (int k = 0; k < 3; k++) diffuse[k] = sys_->bodies()[0].color[k];
        }
        const float ambient[4] = {0, 0, 0, 1}, globalAmbient[4] = {0.12f, 0.12f, 0.14f, 1};
        glLightfv(GL_LIGHT0, GL_POSITION, dir);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
        glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, globalAmbient);
    }

    world::IStarSystem* sys_ = nullptr;
    core::RenderEngine* render_ = nullptr;
    core::IPhysics* physics_ = nullptr;
    bool active_ = false;
    float drawDist_ = 15000;
    int drawn_ = 0;
    std::vector<world::Station> stations_;
    std::vector<world::StationInfo> infos_;
    std::vector<core::BodyId> bodyIds_;
    std::vector<float> farPts_;
    Part cube_, cyl_;
};

REGISTER_MODULE(Stations);
