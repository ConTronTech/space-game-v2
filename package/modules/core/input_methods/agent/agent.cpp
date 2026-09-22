// core/input_methods/agent - a file-driven "device" so an external controller (a script, another process, an AI
// agent) can actually play the game: it contributes to the SAME named actions a real keyboard/joystick would
// ("thrust", "fire", "dock", "toggle_menu", ...), and periodically writes a JSON snapshot of ship/cargo/nearby-ore
// state so the controller can observe before deciding its next move. See docs/AGENT_CONTROL.md.
//
// Protocol (all paths configurable, default under logs/ so they're gitignored and worktree-local):
//   agent.command_file (default logs/agent_cmd.json): a flat JSON object of action -> number, e.g.
//     {"thrust": 1.0, "yaw": -0.3, "fire": 1.0}. Read every frame; a value is CONTRIBUTED EVERY FRAME (held, like a
//     real device) until the file's content changes - write {} to release everything. Missing/invalid file = no
//     input contributed (not an error - the controller just hasn't issued a command yet).
//   agent.control_file (default logs/agent_control.json): one-shot commands, consumed and deleted after acting on
//     them (so they never repeat). Currently: {"craft": "<recipe id>"} calls gameplay::ICrafting::craft directly -
//     crafting is normally a menu click, not a flight input, so this skips simulating menu navigation for it.
//   agent.status_file (default logs/agent_status.json): overwritten agent.status_hz times per second (default 4)
//     with ship/cargo/docking status and the nearest asteroids (id, ore type, distance) - a sensor readout, not a
//     screenshot, so the controller can navigate/decide without needing to read pixels.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include "core/input_handler/input_api.h"
#include "core/input_handler/input_method.h"
#include "core/input_methods/agent/agent_rules.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "gameplay/crafting/crafting_api.h"
#include "gameplay/inventory/inventory_api.h"
#include "ship/docking/docking_api.h"
#include "ship/ship_core/ship_api.h"
#include "world/asteroids/asteroids_api.h"

class AgentInput : public engine::Module, public core::InputMethod {
public:
    const char* name() const override { return "core/input_methods/agent"; }
    int priority() const override { return 200; }   // after the gameplay services it reads/drives (they must be loaded first)
    std::vector<std::string> dependencies() const override { return {"core/input_handler"}; }
    std::vector<std::string> optionalDependencies() const override {
        return {"ship/ship_core", "ship/docking", "gameplay/inventory", "gameplay/crafting", "world/asteroids"};
    }

    bool init(engine::Engine& eng) override {
        eng_ = &eng;
        cmdPath_ = eng.config.get(std::string("agent.command_file"), std::string("logs/agent_cmd.json"), "AI/external-controller input: a flat JSON object of action -> number, held every frame (docs/AGENT_CONTROL.md)");
        ctrlPath_ = eng.config.get(std::string("agent.control_file"), std::string("logs/agent_control.json"), "AI/external-controller one-shot commands, e.g. {\"craft\": \"ore_scanner\"} - consumed and deleted after acting");
        statusPath_ = eng.config.get(std::string("agent.status_file"), std::string("logs/agent_status.json"), "AI/external-controller status snapshot, overwritten agent.status_hz times a second");
        statusHz_ = std::max(0.0f, eng.config.get("agent.status_hz", 4.0f, "how often the status file is rewritten, times per second (0 = never)"));
        in_ = &eng.services.require<core::IInput>();
        in_->registerMethod(this);
        LOG_I("agent", "command %s, control %s, status %s (%.1f Hz)", cmdPath_.c_str(), ctrlPath_.c_str(), statusPath_.c_str(), statusHz_);
        return true;
    }
    void shutdown(engine::Engine&) override { in_->unregisterMethod(this); }

    const char* device() const override { return "agent"; }
    void clearBindings() override {}                                                    // not profile-driven: see the file header
    bool addBinding(const std::string&, const engine::Json&, std::string&) override { return true; }
    void poll(core::IInput& in) override {
        reloadCommandsIfChanged();
        for (auto& [action, value] : commands_) in.contribute(action, value);
    }

    void onUpdate(engine::Engine& eng, float dt) override {
        handleControlFile(eng);
        if (statusHz_ > 0 && (statusTimer_ += dt) >= 1.0f / statusHz_) { statusTimer_ = 0; writeStatus(eng); }
    }

private:
    void reloadCommandsIfChanged() {
        std::string text = readFile(cmdPath_);
        if (text == lastCmdText_) return;   // unchanged (including "file still missing"): keep holding the last values
        lastCmdText_ = text;
        if (text.empty()) { commands_.clear(); return; }
        std::string err;
        engine::Json j = engine::Json::parse(text, &err);
        commands_ = agent::parseCommands(j);
    }

    // {"craft": "<recipe id>"} - the only one-shot command today. The file is deleted right after (success or not),
    // so a controller polling the status file sees the crafting module's own refusal reason if it failed.
    void handleControlFile(engine::Engine& eng) {
        std::string text = readFile(ctrlPath_);
        if (text.empty()) return;
        std::error_code ec;
        std::filesystem::remove(ctrlPath_, ec);   // consume it now: never re-run the same command if writeStatus/poll races the next read
        std::string err;
        engine::Json j = engine::Json::parse(text, &err);
        if (!j.isObject()) { LOG_W("agent", "control file: %s - ignored", err.empty() ? "not an object" : err.c_str()); return; }
        std::string recipe = j["craft"].str();
        if (recipe.empty()) return;
        auto* craft = eng.services.get<gameplay::ICrafting>();
        if (!craft) { LOG_W("agent", "craft '%s' requested but gameplay/crafting is not loaded", recipe.c_str()); return; }
        std::string reason;
        bool ok = craft->craft(recipe, reason);
        LOG_I("agent", "craft '%s': %s (%s)", recipe.c_str(), ok ? "OK" : "refused", reason.c_str());
    }

    void writeStatus(engine::Engine& eng) {
        agent::ShipReport sr;
        auto* ship = eng.services.get<ship::IShip>();
        if (ship) {
            const auto& st = ship->status();
            sr.hp = st.hp; sr.maxHp = st.maxHp; sr.shield = st.shield; sr.maxShield = st.maxShield;
            sr.warpFuel = st.warpFuel; sr.maxWarpFuel = st.maxWarpFuel; sr.alive = st.alive; sr.warping = st.warping; sr.speed = st.speed;
            engine::Vec3d p = ship->positionD(); sr.x = p.x; sr.y = p.y; sr.z = p.z;
            engine::Vec3 v = ship->velocity(); sr.vx = v.x; sr.vy = v.y; sr.vz = v.z;
        }
        agent::DockReport dr;
        if (auto* dock = eng.services.get<ship::IDocking>()) {
            dr.have = true; dr.docked = dock->docked(); dr.stationName = dock->stationName();
            dock->nearestDockable(dr.nearestName, dr.distance, dr.ok, dr.reason);
        }
        agent::CargoReport cr;
        if (auto* inv = eng.services.get<gameplay::IInventory>()) {
            cr.used = inv->used(); cr.capacity = inv->capacity();
            std::vector<gameplay::Stack> stacks;
            inv->stacks(stacks);
            for (auto& s : stacks) cr.stacks.push_back({s.id, s.amount});
            for (const char* perk : {"ore_scanner", "anomaly_scanner"}) if (inv->hasPerk(perk)) cr.perks.push_back(perk);
        }
        std::vector<agent::AsteroidReport> rocks;
        if (auto* ast = eng.services.get<world::IAsteroids>(); ast && ship) {
            world::Vec3d p = ship->positionD();
            std::vector<int> idx;
            ast->nearest(p, 12, idx);
            for (int i : idx) {
                world::Vec3d ap = ast->position(i);
                double dx = ap.x - p.x, dy = ap.y - p.y, dz = ap.z - p.z;
                rocks.push_back({i, ast->ore(i), std::sqrt(dx * dx + dy * dy + dz * dz), ast->radius(i), ap.x, ap.y, ap.z});
            }
        }
        engine::Json j = agent::buildStatus((long)eng.frame(), eng.time(), sr, dr, cr, rocks);
        std::ofstream f(statusPath_, std::ios::trunc);
        if (f) f << j.dump();
    }

    static std::string readFile(const std::string& path) {
        std::ifstream f(path);
        if (!f) return "";
        std::stringstream ss; ss << f.rdbuf();
        return ss.str();
    }

    engine::Engine* eng_ = nullptr;
    core::IInput* in_ = nullptr;
    std::string cmdPath_, ctrlPath_, statusPath_, lastCmdText_;
    std::unordered_map<std::string, float> commands_;
    float statusHz_ = 4.0f, statusTimer_ = 0;
};

REGISTER_MODULE(AgentInput);
