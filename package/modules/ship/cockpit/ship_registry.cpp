#include "ship/cockpit/ship_registry.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "engine/json.h"

namespace cockpit {

namespace fs = std::filesystem;

const ScreenDef* ShipDef::screenForTag(const std::string& tag) const {
    for (auto& s : screens) if (s.tag == tag) return &s;
    return nullptr;
}

bool parseShipJson(const std::string& text, const std::string& folder, ShipDef& out, std::string* error) {
    std::string err;
    engine::Json j = engine::Json::parse(text, &err);
    if (!j.isObject()) {
        if (error) *error = err.empty() ? "expected an object" : err;
        return false;
    }
    out = ShipDef{};
    out.folder = folder;
    out.name = j["name"].str(folder);
    out.model = j["model"].str("ship.obj");
    out.showDefaultUI = j["showDefaultUI"].boolean(true);
    const engine::Json& scr = j["screens"];
    for (size_t i = 0; i < scr.size(); i++) {
        std::string tag = scr.at(i)["tag"].str();
        if (!tag.empty()) out.screens.push_back({tag, scr.at(i)["content"].str()});
    }
    return true;
}

std::vector<ShipDef> scanShips(const std::string& baseDir, std::vector<std::string>* warnings) {
    std::vector<ShipDef> ships;
    std::error_code ec;
    std::vector<fs::path> dirs;
    for (auto& e : fs::directory_iterator(baseDir, ec))
        if (e.is_directory(ec)) dirs.push_back(e.path());
    std::sort(dirs.begin(), dirs.end());
    for (auto& d : dirs) {
        fs::path json = d / "ship.json";
        std::ifstream f(json);
        if (!f) continue;                                    // a folder without ship.json is not a ship
        std::stringstream ss;
        ss << f.rdbuf();
        ShipDef def;
        std::string err;
        if (!parseShipJson(ss.str(), d.filename().string(), def, &err)) {
            if (warnings) warnings->push_back(json.string() + ": " + err);
            continue;
        }
        ships.push_back(std::move(def));
    }
    return ships;
}

const ShipDef* chooseShip(const std::vector<ShipDef>& ships, const std::string& wanted, bool* fellBack) {
    auto lower = [](std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; };
    for (auto& s : ships)
        if (lower(s.name) == lower(wanted)) { if (fellBack) *fellBack = false; return &s; }
    if (fellBack) *fellBack = true;
    return ships.empty() ? nullptr : &ships.front();
}

} // namespace cockpit
