// ship/cockpit - the interior of the ship: an OBJ model drawn in VIEW space (glued to the camera) in cockpit view only,
// plus live content on the model's '@' screens. See docs/COCKPIT.md.
#include <GL/gl.h>
#include <algorithm>
#include <exception>
#include <map>
#include <memory>
#include <set>
#include "core/camera/camera_api.h"
#include "core/import_handler/import_handler.h"
#include "core/render_engine/render_engine.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "ship/cockpit/cockpit_light.h"
#include "ship/cockpit/cockpit_screens_api.h"
#include "ship/cockpit/hud_screens.h"
#include "ship/cockpit/ship_registry.h"

namespace cockpit {

namespace {

struct Batch {
    std::vector<float> positions, normals, colors;   // flat triangle list, rgba colours
    size_t vertexCount() const { return positions.size() / 3; }
};

// Where the light comes from. Nothing in the game has a sun yet; when the world module exists it supplies the star's direction
// through a service and this is the ONE function to change (rotate it into view space here). Direction points TOWARD the light.
struct Light { engine::Vec3 toLight; float intensity; };

} // namespace

class CockpitModule : public engine::Module, public ICockpitScreens {
public:
    const char* name() const override { return "ship/cockpit"; }
    std::vector<std::string> dependencies() const override { return {"core/render_engine", "core/import_handler"}; }
    std::vector<std::string> optionalDependencies() const override { return {"core/camera", "ship/cockpit_dev_ship", "ship/ship_core"}; }

    bool init(engine::Engine& eng) override {
        eng_ = &eng;
        auto& c = eng.config;
        enabled_ = c.get("cockpit.enabled", true, "draw the 3D cockpit model in cockpit view");
        std::string wanted = c.get<std::string>("cockpit.ship", "ShipV2", "ship folder to use: the 'name' in assets/models/ship/*/ship.json (falls back to the first ship found)");
        ambient_ = std::clamp(c.get("cockpit.ambient", 0.25f, "cockpit ambient light, 0 (pitch black shadows) .. 1"), 0.0f, 1.0f);
        intensity_ = std::max(0.0f, c.get("cockpit.light_intensity", 1.0f, "strength of the directional light on the cockpit"));
        std::string dir = c.get<std::string>("cockpit.light_dir", "0.35,0.75,0.55", "direction TOWARD the light in view space, x,y,z (x right, y up, z back); until the world has a sun");
        if (!parseVec3(dir, lightDir_)) LOG_W("cockpit", "cockpit.light_dir '%s' is not three numbers: using 0.35,0.75,0.55", dir.c_str());
        brightness_ = std::clamp(c.get("cockpit.screen_brightness", 1.0f, "brightness of the cockpit screens' content, 0.2 (dim) .. 2"), 0.2f, 2.0f);
        glassScale_ = std::clamp(c.get("cockpit.glass_opacity", 1.0f, "canopy glass opacity multiplier: 0 = invisible glass, 1 = as modelled (30%), 3 = heavy tint"), 0.0f, 3.0f);
        cam_ = eng.services.get<core::ICamera>();

        eng.services.provide<ICockpitScreens>(this);
        hud_ = std::make_unique<HudScreens>(eng);
        registerRenderer("HUD", [this](const ScreenContext& ctx) { hud_->draw(ctx); });

        if (!enabled_) { LOG_I("cockpit", "cockpit.enabled = false: nothing will be drawn"); return true; }
        loadShip(eng, wanted);
        if (ready_) {
            render_ = &eng.services.require<core::RenderEngine>();
            render_->addPass("ship/cockpit", 800, [this](core::RenderEngine&) { drawPass(); });
        }
        return true;   // a missing model is not fatal: the game runs without a cockpit
    }

    void shutdown(engine::Engine& eng) override {
        if (render_) render_->removePass("ship/cockpit");
        eng.services.withdraw<ICockpitScreens>();
    }

    // ---- ICockpitScreens ----
    void registerRenderer(const std::string& group, ScreenRenderer fn) override { renderers_[group] = std::move(fn); }
    void removeRenderer(const std::string& group) override { renderers_.erase(group); }
    bool showsDefaultUI() const override {
        bool ownScreensVisible = ready_ && !showDefaultUI_ && !tagged_.empty() && !(cam_ && cam_->showsShip());
        return !ownScreensVisible;
    }

private:
    void loadShip(engine::Engine& eng, const std::string& wanted) {
        std::vector<std::string> warns;
        auto ships = scanShips("assets/models/ship", &warns);
        for (auto& w : warns) LOG_W("cockpit", "%s", w.c_str());
        bool fellBack = false;
        const ShipDef* def = chooseShip(ships, wanted, &fellBack);
        if (!def) { LOG_W("cockpit", "no ship found under assets/models/ship (a folder with ship.json): no cockpit"); return; }
        if (fellBack) LOG_W("cockpit", "ship '%s' not found, using '%s' instead", wanted.c_str(), def->name.c_str());
        LOG_I("cockpit", "ship '%s' (%s/%s)", def->name.c_str(), def->folder.c_str(), def->model.c_str());

        auto mesh = eng.services.require<core::ImportHandler>().load<core::Mesh>("models/ship/" + def->folder + "/" + def->model);
        if (!mesh || mesh->positions.empty()) {
            LOG_W("cockpit", "cockpit model models/ship/%s/%s did not load: no cockpit will be drawn", def->folder.c_str(), def->model.c_str());
            return;
        }
        buildBatches(*mesh);
        tagged_ = mesh->tagged;
        screens_ = def->screens;
        showDefaultUI_ = def->showDefaultUI;
        ready_ = true;
        LOG_I("cockpit", "%zu triangles, %zu screens, showDefaultUI=%s", mesh->positions.size() / 9, tagged_.size(), showDefaultUI_ ? "true" : "false");
    }

    void buildBatches(const core::Mesh& m) {
        const size_t verts = m.positions.size() / 3;
        const bool colored = m.colors.size() == verts * 4;
        for (size_t t = 0; t + 2 < verts; t += 3) {
            float alpha = colored ? m.colors[t * 4 + 3] : 1.0f;
            const bool isGlass = alpha < 0.99f;
            Batch& b = isGlass ? glass_ : solid_;
            for (size_t k = t; k < t + 3; k++) {
                b.positions.insert(b.positions.end(), m.positions.begin() + (long)k * 3, m.positions.begin() + (long)k * 3 + 3);
                b.normals.insert(b.normals.end(), m.normals.begin() + (long)k * 3, m.normals.begin() + (long)k * 3 + 3);
                if (colored) {
                    b.colors.insert(b.colors.end(), m.colors.begin() + (long)k * 4, m.colors.begin() + (long)k * 4 + 3);
                    b.colors.push_back(isGlass ? std::clamp(alpha * glassScale_, 0.0f, 1.0f) : alpha);
                }
                else b.colors.insert(b.colors.end(), {0.8f, 0.8f, 0.8f, 1.0f});
            }
        }
    }

    Light light() const { return {engine::normalize(lightDir_), intensity_}; }

    void drawBatch(const Batch& b) const {
        if (!b.vertexCount()) return;
        glVertexPointer(3, GL_FLOAT, 0, b.positions.data());
        glNormalPointer(GL_FLOAT, 0, b.normals.data());
        glColorPointer(4, GL_FLOAT, 0, b.colors.data());
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)b.vertexCount());
    }

    void drawPass() {
        if (!ready_) return;
        if (cam_ && cam_->showsShip()) return;   // chase view: the ship is seen from outside, no cockpit

        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();                        // view space: the model rides on the camera

        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_TEXTURE_2D);
        glDepthMask(GL_TRUE);                    // glClear ignores the depth buffer while the mask is off
        glClear(GL_DEPTH_BUFFER_BIT);            // the cockpit never clips into the world
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);

        // lighting: one directional light + ambient
        Light L = light();
        const float pos[4] = {L.toLight.x, L.toLight.y, L.toLight.z, 0.0f};
        const float diff[4] = {L.intensity, L.intensity, L.intensity, 1.0f};
        const float none[4] = {0, 0, 0, 1};
        const float amb[4] = {ambient_, ambient_, ambient_, 1.0f};
        glEnable(GL_LIGHTING);
        for (int i = 1; i < 8; i++) glDisable(GL_LIGHT0 + i);
        glEnable(GL_LIGHT0);
        glLightfv(GL_LIGHT0, GL_POSITION, pos);  // w = 0: a direction, transformed by the (identity) view matrix
        glLightfv(GL_LIGHT0, GL_DIFFUSE, diff);
        glLightfv(GL_LIGHT0, GL_AMBIENT, none);
        glLightfv(GL_LIGHT0, GL_SPECULAR, none);
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
        glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, none);
        glEnable(GL_COLOR_MATERIAL);
        glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_NORMAL_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);

        drawBatch(solid_);

        drawScreens();                           // right after the opaque model, before the see-through glass

        // glass: lit, blended, no depth writes
        glEnable(GL_LIGHTING);
        glEnable(GL_COLOR_MATERIAL);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        drawBatch(glass_);

        glPopMatrix();
        glPopClientAttrib();
        glPopAttrib();
    }

    void drawScreens() {
        if (tagged_.empty()) return;
        glDisable(GL_LIGHTING);
        glDisable(GL_COLOR_MATERIAL);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);

        for (const core::TaggedQuad& q : tagged_) {
            ScreenCanvas canvas(q, brightness_);
            canvas.fill({0.01f, 0.02f, 0.04f, 1.0f});          // dark backing (depth-writing), then the group's content
            auto it = renderers_.find(q.group);
            if (it == renderers_.end()) continue;               // nobody draws this group: a dark, blank screen
            const ScreenDef* def = nullptr;
            for (const ScreenDef& s : screens_) if (s.tag == q.tag) def = &s;
            const std::string& content = def ? def->content : empty_;
            try {
                it->second(ScreenContext{q, canvas, content, (float)eng_->time(), brightness_});
            } catch (const std::exception& e) {
                if (warnedThrow_.insert(q.group).second) LOG_W("cockpit", "renderer for screen group '%s' threw: %s", q.group.c_str(), e.what());
            }
            glDepthMask(GL_FALSE);                              // whatever a renderer did, the next screen starts clean
        }
        glEnableClientState(GL_VERTEX_ARRAY);                   // drawPass expects the arrays on for the glass
        glEnableClientState(GL_NORMAL_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
    }

    engine::Engine* eng_ = nullptr;
    core::RenderEngine* render_ = nullptr;
    core::ICamera* cam_ = nullptr;
    std::unique_ptr<HudScreens> hud_;
    std::map<std::string, ScreenRenderer> renderers_;
    std::set<std::string> warnedThrow_;
    Batch solid_, glass_;
    std::vector<core::TaggedQuad> tagged_;
    std::vector<ScreenDef> screens_;
    std::string empty_;
    bool enabled_ = true, ready_ = false, showDefaultUI_ = true;
    float ambient_ = 0.25f, intensity_ = 1.0f, brightness_ = 1.0f, glassScale_ = 1.0f;
    engine::Vec3 lightDir_{0.35f, 0.75f, 0.55f};
};

REGISTER_MODULE(CockpitModule);

} // namespace cockpit
