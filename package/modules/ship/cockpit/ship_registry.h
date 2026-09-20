#pragma once
// Which ship model the cockpit shows. Every folder under assets/models/ship/ that holds a ship.json is one ship:
//   { "name": "ShipV2", "model": "ShipV2.obj", "showDefaultUI": false,
//     "screens": [ { "tag": "@HUD-INFO", "content": "FLIGHT_DATA" }, ... ] }
// Pure logic (files + JSON, no GL), unit-tested.
#include <string>
#include <vector>

namespace cockpit {

struct ScreenDef { std::string tag, content; };   // '@' material name -> what to show on it

struct ShipDef {
    std::string name;                 // "ShipV2" (defaults to the folder name)
    std::string folder;               // folder name, e.g. "shipv2"
    std::string model = "ship.obj";   // OBJ file inside the folder
    bool showDefaultUI = true;        // false: the model draws its own screens, the flat 2D HUD may hide
    std::vector<ScreenDef> screens;
    const ScreenDef* screenForTag(const std::string& tag) const;   // nullptr if ship.json names none for it
};

// Parses one ship.json. False (with *error) if it is not a JSON object.
bool parseShipJson(const std::string& text, const std::string& folder, ShipDef& out, std::string* error = nullptr);

// Reads <baseDir>/*/ship.json (e.g. "assets/models/ship"), sorted by folder name. Unreadable ones are skipped with a message in *warnings.
std::vector<ShipDef> scanShips(const std::string& baseDir, std::vector<std::string>* warnings = nullptr);

// The ship named 'wanted' (case-insensitive), else the first one, else nullptr when the list is empty. *fellBack tells which.
const ShipDef* chooseShip(const std::vector<ShipDef>& ships, const std::string& wanted, bool* fellBack = nullptr);

} // namespace cockpit
