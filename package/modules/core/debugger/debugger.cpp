// core/debugger - the GAMEPLAY/STATE debugger front end: F6 opens a corner panel (live watch values grouped by module, draw-hook switches,
// the event log), F7 shows/hides every switched-on debug draw, F8 writes logs/debug_state.txt. See docs/DEBUGGER.md.
// The performance twin is core/profiler_overlay (F3-F5). Zero cost while closed: no getter runs, the panel and the pass return at once.
// Everything it shows is optional: modules plug in through core::IDebug (debugger_api.h); the events it logs are subscribed here by type.
#include <GL/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "combat/weapons/weapons_api.h"
#include "core/debugger/debugger_api.h"
#include "core/debugger/debugger_rules.h"
#include "core/input_handler/input_api.h"
#include "core/render_engine/render_engine.h"
#include "core/save_system/save_api.h"
#include "core/ui_handler/ui_handler.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "gameplay/crafting/crafting_api.h"
#include "gameplay/inventory/inventory_api.h"
#include "gameplay/mining/mining_api.h"
#include "ship/docking/docking_api.h"
#include "ship/orbit_lock/orbit_lock_api.h"
#include "ship/ship_core/ship_api.h"

namespace core {

class Debugger : public engine::Module, public IDebug {
public:
    const char* name() const override { return "core/debugger"; }
    std::vector<std::string> dependencies() const override { return {"core/input_handler", "core/render_engine", "core/ui_handler"}; }
    // init after the ship when it is there (the physics watches read ship::IShip lazily anyway, so absence is fine)
    std::vector<std::string> optionalDependencies() const override { return {"ship/ship_core", "ship/fake_ship"}; }

    bool init(engine::Engine& eng) override {
        eng_ = &eng;
        if (!eng.config.get("debugger.enabled", true, "in-game state debugger (F6 panel, F7 debug draws, F8 dump to logs/debug_state.txt); costs nothing until F6")) {
            LOG_I("debugger", "off (debugger.enabled = false)");
            return true;
        }
        enabled_ = true;
        double hz = eng.config.get("debugger.watch_hz", 4.0, "how often the F6 panel re-reads the watch values (per second, only while it is open)");
        watchPeriod_ = 1.0 / std::clamp(hz, 0.5, 60.0);
        int cap = eng.config.get("debugger.log_capacity", 200, "event log lines kept by the debugger (F6 Events tab, F8 dump)");
        log_ = dbg::LogRing((size_t)std::clamp(cap, 10, 5000));

        input_ = eng.services.get<IInput>();
        ui_ = eng.services.get<UIHandler>();
        render_ = eng.services.get<RenderEngine>();
        eng.services.provide<IDebug>(this);

        // dev aids (screenshots without keyboard injection): --debugger-open[=tab], --debug-draw=a,b  --debug-dump=<frame>  --debug-f7=<frame>
        if (eng.hasFlag("debugger-open") || !eng.flagValue("debugger-open").empty()) {
            open_ = true;
            tab_ = std::clamp(std::atoi(eng.flagValue("debugger-open", "0").c_str()), 0, 2);
        }
        std::stringstream draws(eng.flagValue("debug-draw"));
        for (std::string n; std::getline(draws, n, ',');) if (!n.empty()) hooks_.setEnabled(n, true);
        if (!eng.flagValue("debug-dump").empty()) dumpAt_ = std::atol(eng.flagValue("debug-dump").c_str());
        if (!eng.flagValue("debug-f7").empty()) f7At_ = std::atol(eng.flagValue("debug-f7").c_str());

        registerBuiltins();
        subscribeEvents(eng);
        if (ui_) ui_->addPanel("core/debugger", 890, [this](UIHandler& ui) { draw(ui); });
        if (render_) render_->addPass("core/debugger", 190, [this](RenderEngine& r) { drawHooks(r); });
        logEvent("debugger: started");
        LOG_I("debugger", "on: F6 panel, F7 debug draws on/off, F8 dump to logs/debug_state.txt%s", open_ ? " (panel open: --debugger-open)" : "");
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        if (!enabled_) return;
        if (ui_) ui_->removePanel("core/debugger");
        if (render_) render_->removePass("core/debugger");
        eng.services.withdraw<IDebug>();
        enabled_ = false;
    }

    void onUpdate(engine::Engine& eng, float) override {
        if (!enabled_) return;
        if (input_) {
            if (input_->pressed("toggle_debugger")) { open_ = !open_; watchDue_ = 0; }
            if (input_->pressed("toggle_debug_draws")) toggleDraws();
            if (input_->pressed("dump_debug_state")) dump();
        }
        if (f7At_ >= 0 && (long)eng.frame() == f7At_) toggleDraws();
        if (dumpAt_ >= 0 && (long)eng.frame() == dumpAt_) dump();
        if (open_ && eng.time() >= watchDue_) { refresh(); watchDue_ = eng.time() + watchPeriod_; }
    }

    // ---------- IDebug ----------
    void watch(const std::string& n, std::function<std::string()> getter) override {
        if (!watches_.add(n, std::move(getter))) LOG_W("debugger", "watch '%s' registered twice: the newest getter wins", n.c_str());
        watchDue_ = 0;
    }
    void unwatch(const std::string& n) override { watches_.remove(n); watchDue_ = 0; }
    void drawHook(const std::string& n, std::function<void(RenderEngine&)> fn) override {
        if (!hooks_.add(n, std::move(fn))) LOG_W("debugger", "draw hook '%s' registered twice: the newest one wins", n.c_str());
    }
    void removeDrawHook(const std::string& n) override { hooks_.remove(n); }
    void logEvent(const std::string& line) override {
        log_.push("t=" + dbg::fmtTime(eng_ ? eng_->time() : 0.0) + "  " + dbg::oneLine(line));
    }

private:
    using HookFn = std::function<void(RenderEngine&)>;

    void toggleDraws() {
        drawsVisible_ = !drawsVisible_;
        LOG_I("debugger", "debug draws %s (%d switched on)", drawsVisible_ ? "SHOWN" : "hidden", hooks_.enabledCount());
    }

    // ---------- built-in "physics" / "ship" / "engine" watches + the velocity draw (read-only, services looked up lazily) ----------
    ship::IShip* ship() const { return eng_->services.get<ship::IShip>(); }
    void registerBuiltins() {
        auto noShip = std::string("n/a (no ship module)");
        watch("physics.ship_pos", [this, noShip] { auto* s = ship(); if (!s) return noShip; auto p = s->position(); return dbg::fmtVec(p.x, p.y, p.z); });
        watch("physics.ship_vel", [this, noShip] { auto* s = ship(); if (!s) return noShip; auto v = s->velocity(); return dbg::fmtVec(v.x, v.y, v.z) + " m/s"; });
        watch("physics.ship_speed", [this, noShip] {
            auto* s = ship(); if (!s) return noShip;
            auto v = s->velocity(); char b[48]; std::snprintf(b, sizeof b, "%.2f m/s", std::sqrt((double)v.x * v.x + (double)v.y * v.y + (double)v.z * v.z)); return std::string(b); });
        watch("physics.bodies", [] { return std::string("n/a (core::IPhysics has no body count query)"); });
        watch("ship.hull", [this, noShip] { auto* s = ship(); if (!s) return noShip; char b[64]; std::snprintf(b, sizeof b, "%.0f / %.0f", s->status().hp, s->status().maxHp); return std::string(b); });
        watch("ship.shield", [this, noShip] {
            auto* s = ship(); if (!s) return noShip; const auto& st = s->status();
            if (!st.shieldInstalled) return std::string("not installed");
            char b[64]; std::snprintf(b, sizeof b, "%.0f / %.0f%s", st.shield, st.maxShield, st.shieldEnabled ? "" : " (off)"); return std::string(b); });
        watch("ship.warp_fuel", [this, noShip] { auto* s = ship(); if (!s) return noShip; char b[64]; std::snprintf(b, sizeof b, "%.1f / %.1f", s->status().warpFuel, s->status().maxWarpFuel); return std::string(b); });
        watch("ship.state", [this, noShip] { auto* s = ship(); if (!s) return noShip; return std::string(s->status().alive ? "alive" : "DEAD") + (s->status().warping ? ", warping" : ""); });
        watch("engine.time", [this] { char b[64]; std::snprintf(b, sizeof b, "%.2f s (frame %lu)", eng_->time(), eng_->frame()); return std::string(b); });
        watch("engine.paused", [this] { return std::string(eng_->paused() ? "yes" : "no"); });
        drawHook("physics.velocity", [this](RenderEngine& r) { drawVelocity(r); });
    }

    // A line from the ship along its velocity (1 s ahead, at least 5 m long) + a short forward line. Camera-relative in double like ship/orbit_lock's guide.
    void drawVelocity(RenderEngine& r) {
        auto* s = ship();
        if (!s) return;
        const float* v = r.camera.view;
        double cam[3] = {-((double)v[0] * v[12] + (double)v[1] * v[13] + (double)v[2] * v[14]),
                         -((double)v[4] * v[12] + (double)v[5] * v[13] + (double)v[6] * v[14]),
                         -((double)v[8] * v[12] + (double)v[9] * v[13] + (double)v[10] * v[14])};
        float m[16];
        for (int i = 0; i < 16; i++) m[i] = v[i];
        m[12] = m[13] = m[14] = 0;
        auto p = s->position(), vel = s->velocity(), fwd = s->forward();
        double sp = std::sqrt((double)vel.x * vel.x + (double)vel.y * vel.y + (double)vel.z * vel.z);
        double k = sp > 1e-3 ? std::max(1.0, 5.0 / sp) : 0.0;
        double o[3] = {p.x - cam[0], p.y - cam[1], p.z - cam[2]};
        glPushMatrix();
        glLoadMatrixf(m);
        glBegin(GL_LINES);
        if (sp > 1e-3) {
            glColor4f(1.0f, 0.3f, 1.0f, 0.95f);   // magenta: velocity
            glVertex3d(o[0], o[1], o[2]);
            glVertex3d(o[0] + vel.x * k, o[1] + vel.y * k, o[2] + vel.z * k);
        }
        glColor4f(0.3f, 1.0f, 0.4f, 0.8f);         // green: nose direction, 5 m
        glVertex3d(o[0], o[1], o[2]);
        glVertex3d(o[0] + fwd.x * 5.0, o[1] + fwd.y * 5.0, o[2] + fwd.z * 5.0);
        glEnd();
        glPopMatrix();
    }

    // ---------- events the game already emits (typed bus: no generic "every event" hook exists, so each type is listed here) ----------
    void subscribeEvents(engine::Engine& eng) {
        auto& ev = eng.events;
        ev.subscribe<ship::DamageTaken>([this](const ship::DamageTaken& e) {
            char s[160]; std::snprintf(s, sizeof s, "ship: damage %.1f to hull (+%.1f absorbed by shield) from %s", e.amount, e.absorbedByShield, e.source.c_str()); logEvent(s); });
        ev.subscribe<ship::Died>([this](const ship::Died& e) { logEvent("ship: DIED (" + e.cause + ")"); });
        ev.subscribe<ship::Respawned>([this](const ship::Respawned&) { logEvent("ship: respawned"); });
        ev.subscribe<ship::ShieldBroken>([this](const ship::ShieldBroken&) { logEvent("ship: shield broken"); });
        ev.subscribe<ship::FuelEmpty>([this](const ship::FuelEmpty&) { logEvent("ship: warp fuel empty"); });
        ev.subscribe<ship::Docked>([this](const ship::Docked& e) { logEvent("docking: docked at " + e.stationName); });
        ev.subscribe<ship::Undocked>([this](const ship::Undocked& e) { logEvent("docking: undocked from " + e.stationName); });
        ev.subscribe<ship::OrbitLockChanged>([this](const ship::OrbitLockChanged& e) { logEvent(std::string("orbit: ") + (e.locked ? "LOCKED to " : "released from ") + e.bodyName); });
        ev.subscribe<ship::OrbitLockRefused>([this](const ship::OrbitLockRefused& e) { logEvent("orbit: lock refused at " + e.bodyName + ": " + e.reason); });
        ev.subscribe<combat::WeaponChanged>([this](const combat::WeaponChanged& e) { logEvent("combat: weapon " + std::to_string(e.index + 1) + " " + e.name); });
        ev.subscribe<combat::Overheated>([this](const combat::Overheated& e) { logEvent("combat: overheated " + e.name); });
        ev.subscribe<combat::ProjectileHit>([this](const combat::ProjectileHit& e) {
            char s[160]; std::snprintf(s, sizeof s, "combat: hit %s #%d for %.1f (shooter %d)", e.targetKind.c_str(), e.id, e.damage, e.shooter); logEvent(s); });
        ev.subscribe<combat::MissileExploded>([this](const combat::MissileExploded& e) { logEvent("combat: missile exploded (" + e.reason + ")"); });
        ev.subscribe<gameplay::OreMined>([this](const gameplay::OreMined& e) { logEvent("mining: +" + std::to_string(e.amount) + " " + e.ore); });
        ev.subscribe<gameplay::CargoFull>([this](const gameplay::CargoFull& e) { logEvent("inventory: cargo full for " + e.id + " (" + std::to_string(e.refused) + " refused)"); });
        ev.subscribe<gameplay::CraftResult>([this](const gameplay::CraftResult& e) { logEvent("crafting: " + e.recipe + (e.ok ? " ok" : " FAILED: " + e.reason)); });
        ev.subscribe<gameplay::ItemUsed>([this](const gameplay::ItemUsed& e) { logEvent("crafting: used " + e.item); });
        ev.subscribe<GameSaved>([this](const GameSaved& e) { logEvent("save: saved to slot " + e.slot); });
        ev.subscribe<GameLoaded>([this](const GameLoaded& e) { logEvent("save: loaded slot " + e.slot); });
        ev.subscribe<engine::PauseChanged>([this](const engine::PauseChanged& e) { logEvent(e.paused ? "engine: paused" : "engine: unpaused"); });
    }

    // ---------- 4 Hz snapshot ----------
    void refresh() {
        watches_.poll();
        groups_ = dbg::groupByPrefix(watches_.values());
    }

    // ---------- panel ----------
    void draw(UIHandler& ui) {
        if (!open_) { drawToast(ui); return; }
        const Color dim = ui.theme.textDim, txt = ui.theme.text, acc = ui.theme.accent;
        const float W = 440, pad = 12, x = 12, top = 12;
        const float maxH = std::max(200.0f, (float)ui.height() - 24);
        const float lineH = 17;
        ui.rect(x, top, W, maxH, 0.0f, 0.0f, 0.02f, 0.55f);   // darker than plain glass: the panel sits over the HUD's top-left block
        ui.glass(x, top, W, maxH, 0.95f, false, 8);
        float y = top + 8;
        ui.text(x + pad, y, "DEBUGGER", 14, acc);
        ui.text(x + pad + 92, y + 1, std::string("F6 close   F7 draws ") + (drawsVisible_ ? "ON" : "off") + "   F8 dump", 12, dim);
        y += 22;
        int t = ui.tabs({"Watches", "Draws", "Events"}, tab_, x + pad, y, W - 2 * pad, 26);
        if (t != tab_) { tab_ = t; scroll_ = 0; }
        y += 34;
        const float bottom = top + maxH - 30;
        const int rowsFit = std::max(1, (int)((bottom - y) / lineH));

        if (tab_ == 0) {
            int total = 0;
            for (auto& g : groups_) total += 1 + (int)g.rows.size();
            clampScroll(total, rowsFit);
            int row = 0;
            for (auto& g : groups_) {
                if (row >= scroll_ && row < scroll_ + rowsFit) { ui.text(x + pad, y, g.name, 13, acc); y += lineH; }
                row++;
                for (auto& [leaf, val] : g.rows) {
                    if (row >= scroll_ && row < scroll_ + rowsFit) {
                        ui.text(x + pad + 12, y, leaf, 12, dim);
                        ui.text(x + pad + 150, y, shorten(val, 44), 12, txt);
                        y += lineH;
                    }
                    row++;
                }
            }
            if (groups_.empty()) ui.text(x + pad, y, "no watches registered", 12, dim);
        } else if (tab_ == 1) {
            auto names = hooks_.names();
            ui.text(x + pad, y, drawsVisible_ ? "draws VISIBLE (F7 hides them all)" : "draws HIDDEN: press F7 to show the ones switched on", 12, drawsVisible_ ? acc : dim);
            y += 22;
            for (auto& n : names) {
                if (y + 30 > bottom) break;
                bool on = hooks_.enabled(n);
                bool nv = ui.toggle(n, x + pad, y, W - 2 * pad, 28, on, false);
                if (nv != on) { hooks_.setEnabled(n, nv); logEvent("debugger: draw " + n + (nv ? " on" : " off")); }
                y += 32;
            }
            if (names.empty()) ui.text(x + pad, y, "no draw hooks registered", 12, dim);
            else ui.text(x + pad, y + 4, "free the mouse (Tab) to click the switches", 12, dim);
        } else {
            int total = (int)log_.size();
            clampScroll(total, rowsFit);
            // newest at the bottom; scroll_ 0 = the newest page
            int first = std::max(0, total - rowsFit - scroll_);
            int last = std::min(total, first + rowsFit);
            for (int i = first; i < last; i++) { ui.text(x + pad, y, shorten(log_.at((size_t)i), 62), 12, txt); y += lineH; }
            if (total == 0) ui.text(x + pad, y, "no events yet", 12, dim);
        }
        // footer: scroll buttons (clickable with a free mouse) + the dump toast
        float fy = top + maxH - 26;
        if (tab_ != 1) {
            if (ui.button("up", x + W - pad - 104, fy, 50, 20, false)) scroll_ += tab_ == 2 ? rowsFit / 2 : -rowsFit / 2;
            if (ui.button("down", x + W - pad - 50, fy, 50, 20, false)) scroll_ += tab_ == 2 ? -rowsFit / 2 : rowsFit / 2;
            if (scroll_ < 0) scroll_ = 0;
        }
        if (toastActive()) ui.text(x + pad, fy + 2, toast_, 12, acc);
        else ui.text(x + pad, fy + 2, std::to_string(watches_.size()) + " watches, " + std::to_string(log_.size()) + " log lines", 12, dim);
    }
    void clampScroll(int total, int fit) { scroll_ = std::clamp(scroll_, 0, std::max(0, total - fit)); }
    static std::string shorten(const std::string& s, size_t n) { return s.size() <= n ? s : s.substr(0, n - 1) + "~"; }

    void drawToast(UIHandler& ui) {
        if (!toastActive()) return;
        float w = (float)ui.textWidth(toast_, 13) + 24;
        ui.glass(12, 12, w, 30, 0.95f, false, 8);
        ui.text(24, 19, toast_, 13, ui.theme.accent);
    }
    bool toastActive() const { return eng_->time() < toastUntil_; }

    // ---------- the debug-draw pass: GL state owned here, see debugger_api.h for what a hook may assume ----------
    void drawHooks(RenderEngine& r) {
        if (!drawsVisible_ || hooks_.enabledCount() == 0) return;
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadMatrixf(r.camera.view);
        glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_LINE_BIT | GL_CURRENT_BIT);
        glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D); glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
        glLineWidth(1.5f);
        hooks_.forEachEnabled([&](const std::string&, const HookFn& fn) { fn(r); });
        glPopAttrib();
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
    }

    // ---------- F8 ----------
    void dump() {
        refresh();
        dbg::DumpData d;
        char b[128];
        std::snprintf(b, sizeof b, "%s %s", __DATE__, __TIME__);
        d.header.emplace_back("build", b);
        std::snprintf(b, sizeof b, "%.2f s (frame %lu)", eng_->time(), eng_->frame());
        d.header.emplace_back("uptime", b);
        d.header.emplace_back("paused", eng_->paused() ? "yes" : "no");
        d.header.emplace_back("draws", std::string(drawsVisible_ ? "visible" : "hidden") + " (F7)");
        std::snprintf(b, sizeof b, "%zu kept of %lu logged (capacity %zu)", log_.size(), log_.total(), log_.capacity());
        d.header.emplace_back("log", b);
        d.header.emplace_back("performance", "not included: press F5 for logs/profile_live.txt");
        d.watches = watches_.values();
        for (auto& n : hooks_.names()) d.hooks.emplace_back(n, hooks_.enabled(n));
        d.log = log_.all();
        std::error_code ec;
        std::filesystem::create_directories("logs", ec);
        std::ofstream f("logs/debug_state.txt");
        if (f) f << dbg::formatDump(d);
        toast_ = f ? "debug state written to logs/debug_state.txt" : "COULD NOT WRITE logs/debug_state.txt";
        toastUntil_ = eng_->time() + 4.0;
        LOG_I("debugger", "%s", toast_.c_str());
    }

    engine::Engine* eng_ = nullptr;
    IInput* input_ = nullptr;
    UIHandler* ui_ = nullptr;
    RenderEngine* render_ = nullptr;
    bool enabled_ = false, open_ = false, drawsVisible_ = true;
    int tab_ = 0, scroll_ = 0;
    long dumpAt_ = -1, f7At_ = -1;
    double watchPeriod_ = 0.25, watchDue_ = 0, toastUntil_ = 0;
    std::string toast_;
    dbg::WatchRegistry watches_;
    dbg::DrawHooks<HookFn> hooks_;
    dbg::LogRing log_{200};
    std::vector<dbg::Group> groups_;
};

REGISTER_MODULE(Debugger);

} // namespace core
