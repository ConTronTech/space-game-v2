#pragma once
// ui/main_menu's service (docs/MAIN_MENU.md). While isOpen() the engine is held paused and no game session has started:
// the world generated at boot sits frozen underneath. Other menus that also react to "paused" (ui/pause_menu) check
// this so they do not open on top of / respond underneath the main menu.
//     if (auto* mm = eng.services.get<ui::IMainMenu>(); mm && mm->isOpen()) return;
#include <string>

namespace ui {

class IMainMenu {
public:
    virtual ~IMainMenu() = default;
    virtual bool isOpen() const = 0;
};

// Emitted once, when the player leaves the main menu into gameplay (after the save is loaded, for Continue / Load).
struct GameStarted {
    enum class How { NewGame, Continue, Load, Skipped };   // Skipped: the menu was not shown (automated run / disabled)
    How how = How::NewGame;
    std::string slot;                                       // the loaded slot for Continue / Load, else ""
};

} // namespace ui
