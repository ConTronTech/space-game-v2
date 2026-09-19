#include "core/settings/settings.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "engine/engine.h"
#include "engine/log.h"

namespace core {

bool Settings::init(engine::Engine& eng) {
    eng_ = &eng;
    std::string p = eng.flagValue("settings");
    if (!p.empty()) path_ = p;
    load();
    eng.services.provide<ISettings>(this);
    return true;
}

void Settings::shutdown(engine::Engine& eng) {
    if (dirty_) save();
    eng.services.withdraw<ISettings>();
}

bool Settings::load() {
    values_.clear();
    std::ifstream f(path_);
    if (!f) return false;                       // first run: nothing saved yet
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    engine::Json j = engine::Json::parse(ss.str(), &err);
    if (!j.isObject()) { LOG_E("settings", "%s: %s - using defaults", path_.c_str(), err.empty() ? "expected an object" : err.c_str()); return false; }
    for (auto& k : j.keys()) values_[k] = j[k];
    return true;
}

const engine::Json& Settings::raw(const std::string& key) const {
    static const engine::Json none;
    auto it = values_.find(key);
    return it == values_.end() ? none : it->second;
}

void Settings::setRaw(const std::string& key, engine::Json value) {
    auto it = values_.find(key);
    if (it != values_.end() && it->second.dump() == value.dump()) return;   // unchanged
    values_[key] = std::move(value);
    dirty_ = true;
    if (eng_) { lastChange_ = eng_->time(); eng_->events.emit(SettingChanged{key}); }
}

// Debounced autosave: while a slider is being dragged we do not write the file every frame.
void Settings::onUpdate(engine::Engine& eng, float) {
    if (dirty_ && eng.time() - lastChange_ > 0.5) save();
}

void Settings::save() {
    engine::Json o = engine::Json::object();
    for (auto& [k, v] : values_) o.set(k, v);
    std::error_code ec;
    std::filesystem::path p(path_);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::string tmp = path_ + ".tmp";
    {
        std::ofstream f(tmp);
        if (!f) { LOG_E("settings", "cannot write %s", tmp.c_str()); return; }
        f << o.dump();
    }
    std::filesystem::rename(tmp, path_, ec);   // atomic swap: a crash never leaves a half-written file
    if (ec) { LOG_E("settings", "cannot replace %s: %s", path_.c_str(), ec.message().c_str()); return; }
    dirty_ = false;
}

REGISTER_MODULE(Settings);

} // namespace core
