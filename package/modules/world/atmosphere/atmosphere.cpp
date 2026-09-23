// world/atmosphere - a thin glowing shell around every nearby planet: per-vertex alpha from the angle between the shell normal and the
// view direction (bright at the limb/horizon, clear straight down or straight up), dimmed on the night side; plus a faint screen tint
// while the camera is inside a shell. Visual only: no physics, drag, heat or damage. Rules in atmosphere_rules.h; design in docs/ATMOSPHERE.md.
#include <GL/gl.h>
#include <algorithm>
#include <cmath>
#include "core/render_engine/render_engine.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "world/atmosphere/atmosphere_rules.h"
#include "world/star_system/star_system_api.h"
#include "world/star_system/star_system_rules.h"

class Atmosphere : public engine::Module {
public:
    const char* name() const override { return "world/atmosphere"; }
    std::vector<std::string> dependencies() const override { return {"core/render_engine"}; }
    std::vector<std::string> optionalDependencies() const override { return {"world/star_system"}; }

    bool init(engine::Engine& eng) override {
        auto& c = eng.config;
        bool on = c.get("atmosphere.enabled", true, "draw planet atmospheres (rim glow shell + inside tint, visual only, docs/ATMOSPHERE.md)");
        if (eng.hasFlag("force-atmosphere")) on = true;     // test flag: on even where the preset turns it off (Low)
        if (!on) return true;
        p_.rangeFactor = std::max(1.5f, c.get("atmosphere.range_factor", 12.0f, "draw a planet's atmosphere while within this many planet radii of its centre"));
        p_.shellFactor = std::clamp(c.get("atmosphere.shell_radius_factor", 1.4f, "atmosphere shell radius as a multiple of the planet radius (1.035-1.5); user decision 2026-09-22: 40% above the body"), 1.035f, 1.5f);
        p_.insideTintMax = std::clamp(c.get("atmosphere.inside_tint_max", 0.12f, "screen tint alpha at the surface while inside an atmosphere (0 = off)"), 0.0f, 0.5f);
        p_.rimAlpha = std::clamp(c.get("atmosphere.rim_alpha", 0.85f, "atmosphere glow strength at the limb, 0-1"), 0.0f, 1.0f);
        p_.rimPower = std::clamp(c.get("atmosphere.rim_power", 3.0f, "rim falloff exponent: higher = thinner limb, clearer disc"), 0.5f, 8.0f);
        int seg = std::clamp(c.get("atmosphere.segments", 24, "shell slices (stacks = 2/3 of it); vertices = (s+1)(2s/3+1)"), 8, 64);
        shell_ = world::buildSphere(seg, std::max(4, seg * 2 / 3));
        rgba_.resize(shell_.verts.size() / 3 * 4);
        pos_.resize(shell_.verts.size());
        sys_ = eng.services.get<world::IStarSystem>();
        if (!sys_) { LOG_W("atmosphere", "no star system: nothing to draw"); return true; }
        render_ = &eng.services.require<core::RenderEngine>();
        render_->addPass("world/atmosphere", 120, [this](core::RenderEngine& r) { drawShells(r); });   // after all opaque world geometry (ship 110)
        if (p_.insideTintMax > 0) render_->addPass("world/atmosphere_tint", 150, [this](core::RenderEngine&) { drawTint(); });   // before the cockpit (800)
        if (eng.hasFlag("atmosphere-dump"))
            for (const auto& b : sys_->bodies())
                if (b.kind == world::BodyKind::Planet)
                    LOG_I("atmosphere", "%s r=%.0f at (%.0f, %.0f, %.0f)", b.name.c_str(), b.radius, b.position.x, b.position.y, b.position.z);
        active_ = true;
        LOG_I("atmosphere", "on: shell %.3fx, range %.1fx radius, %d shell vertices, inside tint %.2f", p_.shellFactor, p_.rangeFactor,
              (int)shell_.verts.size() / 3, p_.insideTintMax);
        return true;
    }

    void shutdown(engine::Engine&) override {
        if (!active_) return;
        render_->removePass("world/atmosphere");
        render_->removePass("world/atmosphere_tint");
    }

private:
    void drawShells(core::RenderEngine& r) {
        tint_ = 0;
        const auto& bodies = sys_->bodies();
        if (bodies.empty()) return;
        const float* v = r.camera.view;
        // camera position from the view matrix (same derivation as world/star_system): rendering is camera-relative
        const world::Vec3d cam{-((double)v[0] * v[12] + (double)v[1] * v[13] + (double)v[2] * v[14]),
                               -((double)v[4] * v[12] + (double)v[5] * v[13] + (double)v[6] * v[14]),
                               -((double)v[8] * v[12] + (double)v[9] * v[13] + (double)v[10] * v[14])};
        const double clampDist = r.camera.farZ * 0.75;     // star_system draws farther bodies as a depth-less backdrop: no shell there
        const world::Vec3d sun = sys_->sunPosition();
        bool began = false;
        for (const world::Body& b : bodies) {
            if (b.kind != world::BodyKind::Planet) continue;
            const double rx = b.position.x - cam.x, ry = b.position.y - cam.y, rz = b.position.z - cam.z;
            const double dist = std::sqrt(rx * rx + ry * ry + rz * rz);
            const float R = world::shellRadius(b.radius, p_.shellFactor);
            const double range = world::visibleRange(b.radius, p_.rangeFactor);
            if (dist >= range || dist + R >= clampDist) continue;
            const float fade = world::distanceFade(dist, range, p_.fadeStart);
            tint_ = std::max(tint_, world::insideTint(dist, b.radius, R, p_.insideTintMax));
            if (tint_ > 0) world::atmosphereColor(b.color, tintColor_);
            if (!began) { begin(v); began = true; }
            drawShell(b, (float)rx, (float)ry, (float)rz, R, fade, dist < R, sun);
        }
        if (began) end();
    }

    void begin(const float* view) {
        float m[16];
        for (int i = 0; i < 16; i++) m[i] = view[i];
        m[12] = m[13] = m[14] = 0;
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadMatrixf(m);
        glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_POLYGON_BIT | GL_CURRENT_BIT);
        glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D);
        glEnable(GL_DEPTH_TEST);            // the shell is occluded by anything in front of it ...
        glDepthMask(GL_FALSE);              // ... but never hides anything itself
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // additive: a glow that brightens what is behind it, no sorting needed
        glShadeModel(GL_SMOOTH);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
    }
    void end() {
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        glPopAttrib();
        glPopMatrix();
    }

    // Per vertex: camera-relative position, rim alpha from dot(normal, to-camera), sun factor from dot(normal, to-sun).
    void drawShell(const world::Body& b, float cx, float cy, float cz, float R, float fade, bool inside, const world::Vec3d& sun) {
        float sx = (float)(sun.x - b.position.x), sy = (float)(sun.y - b.position.y), sz = (float)(sun.z - b.position.z);
        float sl = std::sqrt(sx * sx + sy * sy + sz * sz);
        if (sl < 1e-3f) sl = 1;
        sx /= sl; sy /= sl; sz /= sl;
        float col[3];
        world::atmosphereColor(b.color, col);
        const int n = (int)shell_.verts.size() / 3;
        for (int i = 0; i < n; i++) {
            const float nx = shell_.verts[i * 3], ny = shell_.verts[i * 3 + 1], nz = shell_.verts[i * 3 + 2];
            const float px = cx + nx * R, py = cy + ny * R, pz = cz + nz * R;       // camera at the origin
            pos_[i * 3] = px; pos_[i * 3 + 1] = py; pos_[i * 3 + 2] = pz;
            const float l = std::sqrt(px * px + py * py + pz * pz);
            const float c = l > 1e-4f ? -(nx * px + ny * py + nz * pz) / l : 1.0f;   // dot(normal, direction to camera)
            const float a = world::rimAlpha(c, p_.rimPower, p_.rimAlpha) * world::sunFactor(nx * sx + ny * sy + nz * sz, p_.nightFloor) * fade;
            rgba_[i * 4] = col[0]; rgba_[i * 4 + 1] = col[1]; rgba_[i * 4 + 2] = col[2]; rgba_[i * 4 + 3] = a;
        }
        // Outside: skip the far half (it would only double the glow over the planet). The sphere's triangles are wound clockwise seen from
        // outside. Inside: every face is the sky, draw both.
        if (inside) glDisable(GL_CULL_FACE);
        else { glEnable(GL_CULL_FACE); glFrontFace(GL_CW); glCullFace(GL_BACK); }
        glVertexPointer(3, GL_FLOAT, 0, pos_.data());
        glColorPointer(4, GL_FLOAT, 0, rgba_.data());
        glDrawElements(GL_TRIANGLES, (GLsizei)shell_.indices.size(), GL_UNSIGNED_SHORT, shell_.indices.data());
    }

    // Faint full-screen wash while inside a shell: a screen-space quad after the 3D world, so no 3D depth testing is changed.
    void drawTint() {
        if (tint_ <= 0.002f) return;
        glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT);
        glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D); glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        glColor4f(tintColor_[0], tintColor_[1], tintColor_[2], tint_);
        glBegin(GL_QUADS);
        glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(1, 1); glVertex2f(-1, 1);
        glEnd();
        glPopMatrix();
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glPopAttrib();
    }

    world::IStarSystem* sys_ = nullptr;
    core::RenderEngine* render_ = nullptr;
    bool active_ = false;
    world::AtmosphereParams p_;
    world::SphereMesh shell_;
    std::vector<float> pos_, rgba_;      // per-frame vertex arrays, reused (no allocation after init)
    float tint_ = 0, tintColor_[3] = {0, 0, 0};
};

REGISTER_MODULE(Atmosphere);
