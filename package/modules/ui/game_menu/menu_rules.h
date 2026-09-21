#pragma once
// Pure state machine for "Esc closes the game menu and must not ALSO open the pause menu" (no SDL/engine types; unit-tested in test_game_menu.cpp).
//
// The pause menu opens on a fresh `pause` press. While the game menu is open a fresh press is turned into a close request AND cancelled for as long
// as the key stays down (otherwise the press would reappear one frame later, when the value returns to 1 against a previous value of 0).
namespace ui {

class PauseSwallow {
public:
    // Call once per frame with the raw (uncancelled) value of the `pause` action and whether the game menu is open.
    // Returns true when the caller must cancel the action this frame (add -1 to it); `closeRequested` is set on the frame the press is first seen.
    bool step(bool menuOpen, float raw, bool& closeRequested) {
        closeRequested = false;
        bool down = raw > 0.5f;
        if (menuOpen && down && !active_) { active_ = true; closeRequested = true; }
        if (active_) {
            if (down) return true;
            active_ = false;
        }
        return false;
    }
    bool active() const { return active_; }
private:
    bool active_ = false;
};

} // namespace ui
