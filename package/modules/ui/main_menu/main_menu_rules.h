#pragma once
// Pure logic for ui/main_menu (no SDL/GL), unit-tested in package/tests/test_main_menu.cpp. See docs/MAIN_MENU.md.
#include <array>
#include <ctime>
#include <string>
#include <vector>
#include "core/save_system/save_api.h"

namespace mainmenu {

enum class Item { Continue, NewGame, Load, Settings, Quit };

inline const char* label(Item i) {
    switch (i) {
        case Item::Continue: return "Continue";
        case Item::NewGame:  return "New Game";
        case Item::Load:     return "Load Game";
        case Item::Settings: return "Settings";
        case Item::Quit:     return "Quit";
    }
    return "?";
}

// Command-line flags that mean "an automated/dev run that expects gameplay to start at once" (smoke test, benchmark,
// scripted scenarios, UI click injection aimed at the pause menu). Any of these skips the main menu unless --main-menu
// forces it. Present = "--flag" or "--flag=value".
inline constexpr std::array<const char*, 14> kAutomationFlags = {
    "frames", "benchmark", "paused", "screenshot", "ui-click", "open-menu", "gravity-scenario",
    "fx-test-loop", "fx-test-thrust", "toast-test", "auto-fire", "auto-gravity", "auto-lock", "auto-weapon",
};

template <class Present>   // Present: bool(const std::string& flag)
bool anyAutomationFlag(Present present) {
    for (const char* f : kAutomationFlags) if (present(std::string(f))) return true;
    return false;
}

// Whether the menu shows at startup. --no-main-menu (forceSkip) always wins, then --main-menu (forceShow), then the
// config switch main_menu.enabled, which automated runs override to "skip".
inline bool shouldShow(bool enabled, bool forceShow, bool forceSkip, bool automation) {
    if (forceSkip) return false;
    if (forceShow) return true;
    return enabled && !automation;
}

// The main page. Continue only when there is something on disk to continue; Load only when a save system exists
// (with zero saves the Load page says so, like the pause menu); Settings only when the pause menu can host the page.
inline std::vector<Item> buildItems(bool hasSaveSystem, bool hasAnySave, bool hasSettings) {
    std::vector<Item> v;
    if (hasSaveSystem && hasAnySave) v.push_back(Item::Continue);
    v.push_back(Item::NewGame);
    if (hasSaveSystem) v.push_back(Item::Load);
    if (hasSettings) v.push_back(Item::Settings);
    v.push_back(Item::Quit);
    return v;
}

// Which slot Continue loads. activeSlot() is session state (empty on every fresh process), so on a normal launch this
// is the newest save on disk - listSlots() is newest first and includes the autosave, which is what "carry on where I
// stopped" means. A non-empty active slot that still exists wins (only reachable if the menu is ever reopened
// mid-session). "" = nothing to continue.
inline std::string continueSlot(const std::string& activeSlot, const std::vector<core::SlotInfo>& slotsNewestFirst) {
    if (!activeSlot.empty())
        for (const auto& s : slotsNewestFirst) if (s.name == activeSlot) return activeSlot;
    return slotsNewestFirst.empty() ? std::string() : slotsNewestFirst.front().name;
}

// Keyboard/controller focus: wraps around, safe for n <= 0.
inline int moveFocus(int focus, int dir, int n) {
    if (n <= 0) return 0;
    return ((focus + dir) % n + n) % n;
}

// "Autosave  2026-09-22  01:30:00" / "2026-09-22  01:30:00" (local time), the same text the pause menu's Load page shows.
inline std::string slotLabel(const core::SlotInfo& s) {
    std::time_t tt = (std::time_t)s.time;
    char b[32] = "";
    if (const std::tm* tm = std::localtime(&tt)) std::strftime(b, sizeof b, "%Y-%m-%d  %H:%M:%S", tm);
    return (s.name == core::ISaveSystem::kAutosaveSlot ? std::string("Autosave  ") : std::string()) + b;
}

} // namespace mainmenu
