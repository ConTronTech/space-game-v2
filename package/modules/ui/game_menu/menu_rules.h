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

// ---- the CARGO grid (docs/INVENTORY.md "CARGO grid") ----
// Keyboard / D-pad cursor: one step in a direction, clamped at the grid edges (no wrap, so holding a direction stops at the edge).
// A negative slot (nothing selected yet) starts at slot 0.
inline int gridStep(int slot, int dx, int dy, int cols, int count) {
    if (count <= 0 || cols <= 0) return -1;
    if (slot < 0 || slot >= count) return 0;
    int col = slot % cols, row = slot / cols, rows = (count + cols - 1) / cols;
    col = col + dx < 0 ? 0 : (col + dx >= cols ? cols - 1 : col + dx);
    row = row + dy < 0 ? 0 : (row + dy >= rows ? rows - 1 : row + dy);
    int s = row * cols + col;
    return s < count ? s : count - 1;                                    // a short last row
}

// The non-mouse discard: the first press on a slot ARMS it, a second press on the SAME slot within `window` seconds discards.
// Pressing on another slot re-arms that one instead. (A controller paddle must not trash a stack with one accidental press.)
class DiscardConfirm {
public:
    // Returns true when the slot should be discarded now.
    bool press(int slot, double now, double window = 3.0) {
        if (slot < 0) { armed_ = -1; return false; }
        if (armed_ == slot && now - armedAt_ <= window) { armed_ = -1; return true; }
        armed_ = slot; armedAt_ = now;
        return false;
    }
    bool armed(int slot, double now, double window = 3.0) const { return armed_ >= 0 && armed_ == slot && now - armedAt_ <= window; }
    void reset() { armed_ = -1; }
private:
    int armed_ = -1;
    double armedAt_ = 0;
};

} // namespace ui
