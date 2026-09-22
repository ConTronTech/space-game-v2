#pragma once
// Pure blueprint rules: no GL, no SDL, no engine services. Unit-tested in package/tests/test_blueprints.cpp. Design: docs/BLUEPRINTS.md.
// The unlocked set, its save format ({"unlocked": ["id", ...]}) and the display-name fallback.
#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>
#include "engine/json.h"

namespace gameplay {

// Trim spaces; ids are otherwise kept as written (data ids are lower_snake_case).
inline std::string blueprintId(const std::string& raw) {
    size_t a = 0, b = raw.size();
    while (a < b && std::isspace((unsigned char)raw[a])) a++;
    while (b > a && std::isspace((unsigned char)raw[b - 1])) b--;
    return raw.substr(a, b - a);
}

// "missile_pack" -> "Missile Pack" (used when data/blueprints.json has no name for the id)
inline std::string blueprintTitle(const std::string& id) {
    std::string out;
    bool up = true;
    for (char c : id) {
        if (c == '_' || c == '-' || c == ' ') { if (!out.empty() && out.back() != ' ') out += ' '; up = true; continue; }
        out += up ? (char)std::toupper((unsigned char)c) : c;
        up = false;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out.empty() ? id : out;
}

// data/blueprints.json entry ({"name": "..."}) -> display name
inline std::string blueprintName(const std::string& id, const engine::Json& entry) {
    std::string n = blueprintId(entry["name"].str());
    return n.empty() ? blueprintTitle(id) : n;
}

class BlueprintSet {
public:
    bool unlocked(const std::string& id) const { return ids_.count(blueprintId(id)) > 0; }
    bool unlock(const std::string& raw) {
        std::string id = blueprintId(raw);
        return !id.empty() && ids_.insert(id).second;
    }
    std::vector<std::string> list() const { return {ids_.begin(), ids_.end()}; }
    size_t size() const { return ids_.size(); }
    void clear() { ids_.clear(); }

    engine::Json toJson() const {
        engine::Json a = engine::Json::array();
        for (auto& id : ids_) a.push(id);
        return engine::Json::object().set("unlocked", a);
    }
    // Replaces the set. Non-string / empty entries and duplicates are ignored; a missing key = nothing unlocked.
    void fromJson(const engine::Json& j) {
        ids_.clear();
        const engine::Json& a = j["unlocked"];
        for (size_t i = 0; i < a.size(); i++) unlock(a.at(i).str());
    }

private:
    std::set<std::string> ids_;
};

} // namespace gameplay
