#pragma once
// The game menu: a tabbed overlay (I key) other modules add tabs to, the way panels are registered.
//
//     if (auto* menu = eng.services.get<ui::IGameMenu>())
//         menu->addTab("MAP", 30, [](core::UIHandler& ui, float x, float y, float w, float h) { /* draw inside the rectangle */ });
//     ...and in shutdown: menu->removeTab("MAP");
//
// Built-in tabs: CARGO (order 10, ui/game_menu) and CRAFTING (order 20, gameplay/crafting). Tabs are sorted by `order`.
#include <functional>
#include <string>

namespace core { class UIHandler; }

namespace ui {

// x, y, w, h = the content rectangle of the tab (below the tab row), in pixels.
using TabDraw = std::function<void(core::UIHandler&, float x, float y, float w, float h)>;

class IGameMenu {
public:
    virtual ~IGameMenu() = default;
    virtual void addTab(const std::string& name, int order, TabDraw draw) = 0;   // replaces a tab of the same name
    virtual void removeTab(const std::string& name) = 0;
    virtual void open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
};

} // namespace ui
