#pragma once
// Player preferences (FOV, fullscreen, volumes, mouse sensitivity...), saved to config/settings.json.
// Different from engine.config (config/game.json): that is hand-edited tuning; this is written by the
// game when the player changes something in a menu.
//
//     auto* s = eng.services.get<core::ISettings>();                 // may be null if the module is off
//     float fov = s ? s->get("video.fov", 90.0f) : 90.0f;            // default lives with the caller
//     s->set("video.fov", 100.0f);                                   // saved soon after, SettingChanged emitted
//     eng.events.subscribe<core::SettingChanged>([](const core::SettingChanged& e){ if (e.key == "video.fov") ... });
//
// Convention: the module that owns a setting reads it in init(), listens for SettingChanged and applies it.
// Menus only call set().
#include <string>
#include <type_traits>
#include "engine/json.h"

namespace core {

struct SettingChanged { std::string key; };

class ISettings {
public:
    virtual ~ISettings() = default;
    virtual const engine::Json& raw(const std::string& key) const = 0;   // null Json when unset
    virtual void setRaw(const std::string& key, engine::Json value) = 0;
    virtual void save() = 0;

    template <class T> T get(const std::string& key, T def) const {
        const engine::Json& j = raw(key);
        if constexpr (std::is_same_v<T, bool>) return j.boolean(def);
        else if constexpr (std::is_arithmetic_v<T>) return (T)j.num((double)def);
        else return j.str(def);
    }
    template <class T> void set(const std::string& key, T value) { setRaw(key, engine::Json(value)); }
};

} // namespace core
