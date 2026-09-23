#pragma once
// ui/pause_menu's service: lets another screen (ui/main_menu) reuse the pause menu's Settings page instead of keeping a
// second copy of it. openSettingsOnly() shows just the Settings page; Back / Esc closes it again (it never falls through
// to the PAUSED page). Only drawn while the engine is paused, which the main menu always is.
namespace ui {

class IPauseMenu {
public:
    virtual ~IPauseMenu() = default;
    virtual void openSettingsOnly() = 0;
    virtual bool settingsOnlyOpen() const = 0;
};

} // namespace ui
