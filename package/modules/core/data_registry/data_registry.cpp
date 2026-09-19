#include "core/data_registry/data_registry.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "engine/engine.h"
#include "engine/log.h"

namespace fs = std::filesystem;

namespace core {

bool DataRegistry::init(engine::Engine& eng) {
    eng_ = &eng;
    std::string d = eng.flagValue("data");
    if (!d.empty()) dir_ = d;
    reload();
    eng.services.provide<IData>(this);
    return true;   // a game with no data/ still runs; consumers see empty categories
}

void DataRegistry::shutdown(engine::Engine& eng) { eng.services.withdraw<IData>(); }

bool DataRegistry::reload() {
    cats_.clear();
    std::error_code ec;
    if (!fs::is_directory(dir_, ec)) { LOG_W("data", "no data folder '%s'", dir_.c_str()); return false; }

    std::vector<fs::path> entries;
    for (auto& e : fs::directory_iterator(dir_, ec)) entries.push_back(e.path());
    std::sort(entries.begin(), entries.end());

    for (auto& p : entries) {
        if (fs::is_directory(p, ec)) {
            std::vector<fs::path> files;
            for (auto& f : fs::directory_iterator(p, ec)) if (f.path().extension() == ".json") files.push_back(f.path());
            std::sort(files.begin(), files.end());                    // alphabetical: later files override earlier ones
            for (auto& f : files) loadFile(f.string(), p.filename().string());
        } else if (p.extension() == ".json") {
            loadFile(p.string(), p.stem().string());
        }
    }
    size_t defs = 0;
    for (auto& [n, c] : cats_) defs += c.defs.size();
    LOG_I("data", "loaded %zu definitions in %zu categories from %s", defs, cats_.size(), dir_.c_str());
    if (eng_) eng_->events.emit(DataReloaded{});
    return !cats_.empty();
}

void DataRegistry::loadFile(const std::string& path, const std::string& category) {
    std::ifstream f(path);
    if (!f) { LOG_E("data", "cannot open %s", path.c_str()); return; }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    engine::Json j = engine::Json::parse(ss.str(), &err);
    if (!j.isObject()) { LOG_E("data", "%s: %s - file skipped", path.c_str(), err.empty() ? "expected an object of id -> definition" : err.c_str()); return; }

    Category& c = cats_[category];
    for (auto& id : j.keys()) {
        if (!id.empty() && id[0] == '_') continue;                     // comment key
        const engine::Json& def = j[id];
        if (!def.isObject()) { LOG_W("data", "%s: '%s' is not an object - skipped", path.c_str(), id.c_str()); continue; }
        if (c.defs.count(id)) LOG_D("data", "%s: '%s' overrides an earlier definition", path.c_str(), id.c_str());
        else c.order.push_back(id);
        c.defs[id] = def;
    }
}

bool DataRegistry::has(const std::string& category, const std::string& id) const {
    auto c = cats_.find(category);
    return c != cats_.end() && c->second.defs.count(id);
}

const engine::Json& DataRegistry::get(const std::string& category, const std::string& id) const {
    static const engine::Json none;
    auto c = cats_.find(category);
    if (c == cats_.end()) return none;
    auto d = c->second.defs.find(id);
    return d == c->second.defs.end() ? none : d->second;
}

std::vector<std::string> DataRegistry::ids(const std::string& category) const {
    auto c = cats_.find(category);
    return c == cats_.end() ? std::vector<std::string>{} : c->second.order;
}

std::vector<std::string> DataRegistry::categories() const {
    std::vector<std::string> out;
    for (auto& [n, c] : cats_) out.push_back(n);
    return out;
}

REGISTER_MODULE(DataRegistry);

} // namespace core
