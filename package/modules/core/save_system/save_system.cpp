#include "core/save_system/save_system.h"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "engine/engine.h"
#include "engine/log.h"

namespace fs = std::filesystem;

namespace core {

bool SaveSystem::init(engine::Engine& eng) {
    eng_ = &eng;
    std::string d = eng.flagValue("saves");
    if (!d.empty()) dir_ = d;
    autosaveInterval_ = eng.config.get("save.autosave_interval_seconds", 300.0f,
                                       "autosave every N seconds of play (pause-menu time does not count) into saves/autosave.json; 0 = off");
    autosaveTimer_ = 0;
    eng.services.provide<ISaveSystem>(this);
    return true;
}

void SaveSystem::shutdown(engine::Engine& eng) { eng.services.withdraw<ISaveSystem>(); }

long long SaveSystem::now() const { return clock_ ? clock_() : (long long)std::time(nullptr); }

// Slot names become file names: allow only letters, digits, '_' and '-' (no '/', '..', etc).
bool SaveSystem::validSlotName(const std::string& n) {
    if (n.empty() || n.size() > 64) return false;
    return std::all_of(n.begin(), n.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; });
}

void SaveSystem::registerSaveable(ISaveable* s) {
    for (auto*& e : saveables_) {
        if (std::string(e->saveId()) == s->saveId()) {
            LOG_W("save", "duplicate saveable id '%s' - replacing the earlier one", s->saveId());
            e = s;
            return;
        }
    }
    saveables_.push_back(s);
}

void SaveSystem::unregisterSaveable(ISaveable* s) {
    saveables_.erase(std::remove(saveables_.begin(), saveables_.end(), s), saveables_.end());
}

bool SaveSystem::saveSlot(const std::string& name) {
    if (!writeSlot(name)) return false;
    if (name != kAutosaveSlot) active_ = name;   // saving into the autosave slot by hand still leaves "Save" pointing where it was
    return true;
}

bool SaveSystem::autosave() {
    return writeSlot(kAutosaveSlot);             // never touches active_
}

void SaveSystem::onFixedUpdate(engine::Engine&, float dt) {
    if (autosaveInterval_ <= 0) return;
    autosaveTimer_ += dt;
    if (autosaveTimer_ < autosaveInterval_) return;
    autosaveTimer_ = 0;                          // restart even on failure: the error is logged once per interval, not every step
    if (autosave()) LOG_I("save", "autosaved");
}

bool SaveSystem::writeSlot(const std::string& name) {
    if (!validSlotName(name)) { LOG_E("save", "invalid slot name '%s'", name.c_str()); return false; }

    engine::Json mods = engine::Json::object();
    for (auto* s : saveables_) mods.set(s->saveId(), s->save());
    engine::Json root = engine::Json::object();
    root.set("format", kFormat).set("time", now()).set("modules", std::move(mods));

    std::error_code ec;
    fs::create_directories(dir_, ec);
    std::string path = pathFor(name), tmp = path + ".tmp";
    {
        std::ofstream f(tmp);
        if (!f) { LOG_E("save", "cannot write %s", tmp.c_str()); return false; }
        f << root.dump();
        if (!f) { LOG_E("save", "write failed for %s (disk full?)", tmp.c_str()); fs::remove(tmp, ec); return false; }
    }
    fs::rename(tmp, path, ec);   // atomic: a crash mid-save never destroys the previous save
    if (ec) { LOG_E("save", "cannot replace %s: %s", path.c_str(), ec.message().c_str()); return false; }
    LOG_I("save", "saved '%s' (%zu modules)", name.c_str(), saveables_.size());
    lastSave_ = now();
    autosaveTimer_ = 0;                          // a fresh save (manual or auto) restarts the autosave countdown
    if (eng_) eng_->events.emit(GameSaved{name});
    return true;
}

bool SaveSystem::loadSlot(const std::string& name) {
    if (!validSlotName(name)) { LOG_E("save", "invalid slot name '%s'", name.c_str()); return false; }
    std::ifstream f(pathFor(name));
    if (!f) { LOG_E("save", "cannot open %s", pathFor(name).c_str()); return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    engine::Json root = engine::Json::parse(ss.str(), &err);
    if (!root.isObject() || !root["modules"].isObject()) {
        LOG_E("save", "%s is not a valid save (%s)", name.c_str(), err.empty() ? "no modules section" : err.c_str());
        return false;                                   // nothing was touched
    }
    int fmt = (int)root["format"].num(0);
    if (fmt < 1 || fmt > kFormat) { LOG_E("save", "%s has unsupported format %d (this build reads up to %d)", name.c_str(), fmt, kFormat); return false; }

    const engine::Json& mods = root["modules"];
    int loaded = 0;
    for (auto* s : saveables_) {
        if (!mods.has(s->saveId())) { LOG_W("save", "'%s' has no data for %s - keeping its current state", name.c_str(), s->saveId()); continue; }
        s->load(mods[s->saveId()]);
        loaded++;
    }
    for (auto& id : mods.keys()) {
        bool known = std::any_of(saveables_.begin(), saveables_.end(), [&](ISaveable* s) { return id == s->saveId(); });
        if (!known) LOG_D("save", "'%s' contains data for unknown module %s (ignored)", name.c_str(), id.c_str());
    }
    LOG_I("save", "loaded '%s' (%d modules)", name.c_str(), loaded);
    lastLoad_ = now();
    autosaveTimer_ = 0;
    // Loading a slot makes it the one "Save" overwrites. Loading the autosave does not: the next "Save" then acts like
    // "Save As" (a new slot) instead of writing into the slot the timer will overwrite anyway.
    active_ = name == kAutosaveSlot ? std::string() : name;
    if (eng_) eng_->events.emit(GameLoaded{name});
    return true;
}

std::vector<SlotInfo> SaveSystem::listSlots() const {
    std::vector<SlotInfo> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir_, ec)) {
        if (!e.is_regular_file() || e.path().extension() != ".json") continue;
        std::string name = e.path().stem().string();
        if (!validSlotName(name)) continue;
        std::ifstream f(e.path());
        std::stringstream ss;
        ss << f.rdbuf();
        engine::Json j = engine::Json::parse(ss.str());
        if (!j.isObject()) continue;                    // skip corrupt files instead of listing them
        out.push_back({name, (long long)j["time"].num(0)});
    }
    std::sort(out.begin(), out.end(), [](const SlotInfo& a, const SlotInfo& b) {
        return a.time != b.time ? a.time > b.time : a.name > b.name;
    });
    return out;
}

bool SaveSystem::deleteSlot(const std::string& name) {
    if (!validSlotName(name)) return false;
    std::error_code ec;
    bool removed = fs::remove(pathFor(name), ec);
    if (removed && name == active_) active_.clear();   // "Save" must not silently recreate a slot the player deleted
    return removed;
}

std::string SaveSystem::newSlotName() const {
    std::time_t t = (std::time_t)now();
    char b[32];
    std::strftime(b, sizeof b, "save_%Y%m%d_%H%M%S", std::localtime(&t));
    std::string base = b, name = base;
    for (int i = 2; fs::exists(pathFor(name)); i++) name = base + "_" + std::to_string(i);
    return name;
}

REGISTER_MODULE(SaveSystem);

} // namespace core
