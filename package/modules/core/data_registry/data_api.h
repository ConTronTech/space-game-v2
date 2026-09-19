#pragma once
// Game content as data. Items, ores, recipes, ship specs... live as JSON in data/, not in code, so
// adding one is adding a few lines of JSON.
//
//     auto& data = eng.services.require<core::IData>();
//     for (auto& id : data.ids("ores")) {
//         const engine::Json& ore = data.get("ores", id);
//         float rarity = (float)ore["rarity"].num(1.0);
//     }
//
// Layout: every folder under data/ (data/ores/*.json) or file (data/ores.json) is a CATEGORY. A file is an object of
//   "id": { ...definition... }
// Keys starting with "_" are comments and ignored. Files load in alphabetical order and a later file
// REPLACES an earlier definition with the same id, so data/ores/zz_mod.json can add or override ores.
#include <string>
#include <vector>
#include "engine/json.h"

namespace core {

struct DataReloaded {};

class IData {
public:
    virtual ~IData() = default;
    virtual bool has(const std::string& category, const std::string& id) const = 0;
    virtual const engine::Json& get(const std::string& category, const std::string& id) const = 0;   // null Json if missing
    virtual std::vector<std::string> ids(const std::string& category) const = 0;                      // definition order
    virtual std::vector<std::string> categories() const = 0;
    virtual bool reload() = 0;   // re-scan data/ (emits DataReloaded); false if nothing could be loaded
};

} // namespace core
