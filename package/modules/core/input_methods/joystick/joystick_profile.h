#pragma once
// Reading device profiles from JSON (config/input/devices/*.json). Format and an annotated example: docs/CONTROLLERS.md.
#include <cstdlib>
#include <string>
#include "engine/json.h"
#include "core/input_methods/joystick/joystick_rules.h"

namespace joystick {

inline bool parseRole(const std::string& s, Role& out) {
    if (s.empty() || s == "stick") out = Role::Stick;
    else if (s == "steering") out = Role::Steering;
    else if (s == "throttle") out = Role::Throttle;
    else if (s == "brake") out = Role::Brake;
    else if (s == "clutch") out = Role::Clutch;
    else if (s == "combined_pedals") out = Role::CombinedPedals;
    else return false;
    return true;
}

inline int parseHex(const std::string& s) {
    if (s.empty()) return -1;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 16);
    return (end && *end == 0 && v >= 0 && v <= 0xffff) ? (int)v : -1;
}

// Returns false with `err` set for a file that cannot be used; the caller logs it and falls back (never crashes).
inline bool parseProfile(const engine::Json& j, Profile& out, std::string& err) {
    if (!j.isObject()) { err = "profile is not a JSON object"; return false; }
    Profile p;
    p.name = j["name"].str("unnamed");
    p.status = j["status"].str();
    const engine::Json& m = j["match"];
    p.match.vid = parseHex(m["vid"].str());
    p.match.pid = parseHex(m["pid"].str());
    p.match.nameContains = m["name_contains"].str();
    if (m.has("vid") && p.match.vid < 0) { err = "match.vid must be a hex string like \"11ff\""; return false; }
    if (m.has("pid") && p.match.pid < 0) { err = "match.pid must be a hex string like \"3245\""; return false; }

    const engine::Json& axes = j["axes"];
    for (size_t i = 0; i < axes.size(); i++) {
        const engine::Json& a = axes.at(i);
        AxisMap am;
        if (!a.has("index")) { err = "axes[" + std::to_string(i) + "]: missing \"index\""; return false; }
        am.index = (int)a["index"].num(-1);
        if (am.index < 0 || am.index > 63) { err = "axes[" + std::to_string(i) + "]: bad index"; return false; }
        if (!parseRole(a["role"].str(), am.role)) { err = "axes[" + std::to_string(i) + "]: unknown role '" + a["role"].str() + "'"; return false; }
        am.action = a["action"].str();
        am.above = a["above"].str("thrust"); am.below = a["below"].str("brake");
        am.scale = (float)a["scale"].num(1.0);
        am.invert = a["invert"].boolean(false);
        am.deadzone = std::clamp((float)a["deadzone"].num(0.0), 0.0f, 0.95f);
        am.curve = (float)a["curve"].num(1.0);
        const engine::Json& c = a["calibration"];
        am.cal.min = (int)c["min"].num(-32768); am.cal.max = (int)c["max"].num(32767); am.cal.centre = (int)c["centre"].num(0);
        if (am.cal.max <= am.cal.min) { err = "axes[" + std::to_string(i) + "]: calibration max must be above min"; return false; }
        std::string rest = a["rest"].str("min");
        if (rest != "min" && rest != "max") { err = "axes[" + std::to_string(i) + "]: rest must be \"min\" or \"max\""; return false; }
        am.rest = rest == "max" ? Rest::Max : Rest::Min;
        if (am.role != Role::CombinedPedals && am.action.empty()) { err = "axes[" + std::to_string(i) + "]: missing \"action\""; return false; }
        p.axes.push_back(am);
    }
    const engine::Json& buttons = j["buttons"];
    for (size_t i = 0; i < buttons.size(); i++) {
        const engine::Json& b = buttons.at(i);
        ButtonMap bm;
        bm.index = (int)b["index"].num(-1);
        bm.action = b["action"].str();
        if (bm.index < 0 || bm.index > 255 || bm.action.empty()) { err = "buttons[" + std::to_string(i) + "]: needs index and action"; return false; }
        bm.scale = (float)b["scale"].num(1.0);
        std::string mode = b["mode"].str("hold");
        if (mode == "toggle") bm.mode = ButtonMode::Toggle;
        else if (mode == "hold" || mode == "press") bm.mode = ButtonMode::Hold;
        else { err = "buttons[" + std::to_string(i) + "]: mode must be hold, press or toggle"; return false; }
        p.buttons.push_back(bm);
    }
    const engine::Json& hats = j["hats"];
    for (size_t i = 0; i < hats.size(); i++) {
        const engine::Json& h = hats.at(i);
        HatMap hm;
        hm.index = (int)h["index"].num(0);
        hm.up = h["up"].str(); hm.down = h["down"].str(); hm.left = h["left"].str(); hm.right = h["right"].str();
        p.hats.push_back(hm);
    }
    const engine::Json& groups = j["groups"];
    for (size_t i = 0; i < groups.size(); i++) {
        const engine::Json& g = groups.at(i);
        GroupMap gm;
        gm.action = g["action"].str();
        if (gm.action.empty()) { err = "groups[" + std::to_string(i) + "]: missing action"; return false; }
        const engine::Json& bs = g["buttons"];
        for (auto& key : bs.keys()) gm.buttons.push_back({std::atoi(key.c_str()), (float)bs[key].num(0.0)});
        if (g.has("default")) { gm.hasDefault = true; gm.defaultValue = (float)g["default"].num(0.0); }
        p.groups.push_back(gm);
    }
    out = p;
    return true;
}

} // namespace joystick
