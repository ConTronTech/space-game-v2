// joytest - a standalone visual joystick / wheel / pedal / shifter tester (SDL2 only, no game code, no fonts needed).
//   ./joytest            open the window; press / move each control one at a time and watch it light up
//   ./joytest --guided [--dev=N]   guided mode: asks for one control at a time, writes logs/joytest_guided.json
//   keys: R = reset min/max, S = save the summary now, ESC or Q = quit (the summary is also written on quit)
// Writes logs/joytest.log (timestamped events) and logs/joytest_summary.txt (per axis: rest, min, max, moves; per button: presses).
// See docs/CONTROLLERS.md. Build: `make joytest` (SDL2 only).
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

// ---- 5x7 font (uppercase, digits, a few symbols) ----
struct Glyph { char c; unsigned char rows[7]; };
static const Glyph kFont[] = {
    {'0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}}, {'1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}}, {'2', {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}},
    {'3', {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}}, {'4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}}, {'5', {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
    {'6', {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}}, {'7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}}, {'8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},
    {'9', {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
    {'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}}, {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}}, {'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}},
    {'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}}, {'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}}, {'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
    {'G', {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}}, {'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}}, {'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}},
    {'J', {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}}, {'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11}}, {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'M', {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}}, {'N', {0x11,0x19,0x15,0x13,0x11,0x11,0x11}}, {'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}}, {'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}}, {'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}}, {'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}}, {'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}}, {'W', {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}}, {'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
    {'Y', {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}}, {'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    {'-', {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}}, {'+', {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}}, {'.', {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}},
    {':', {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}}, {'=', {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}}, {'/', {0x01,0x01,0x02,0x04,0x08,0x10,0x10}},
    {'(', {0x02,0x04,0x08,0x08,0x08,0x04,0x02}}, {')', {0x08,0x04,0x02,0x02,0x02,0x04,0x08}}, {'#', {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A}},
    {'_', {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}}, {'>', {0x10,0x08,0x04,0x02,0x04,0x08,0x10}}, {'<', {0x01,0x02,0x04,0x08,0x04,0x02,0x01}},
    {'%', {0x19,0x19,0x02,0x04,0x08,0x13,0x13}}, {',', {0x00,0x00,0x00,0x00,0x0C,0x04,0x08}},
};

static SDL_Renderer* g_r = nullptr;

static void text(int x, int y, const std::string& s, int scale, Uint8 r, Uint8 g, Uint8 b) {
    SDL_SetRenderDrawColor(g_r, r, g, b, 255);
    for (char ch : s) {
        char c = (char)std::toupper((unsigned char)ch);
        if (c != ' ') for (const auto& gl : kFont) if (gl.c == c) {
            for (int row = 0; row < 7; row++) for (int col = 0; col < 5; col++)
                if (gl.rows[row] & (0x10 >> col)) { SDL_Rect p{x + col * scale, y + row * scale, scale, scale}; SDL_RenderFillRect(g_r, &p); }
            break;
        }
        x += 6 * scale;
    }
}
static void rect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, bool fill = true) {
    SDL_SetRenderDrawColor(g_r, r, g, b, 255);
    SDL_Rect q{x, y, w, h};
    if (fill) SDL_RenderFillRect(g_r, &q); else SDL_RenderDrawRect(g_r, &q);
}
static std::string num(int v, bool sign = false) { char b[24]; std::snprintf(b, sizeof b, sign ? "%+d" : "%d", v); return b; }

struct AxisState { int rest = 0, cur = 0, mn = 32767, mx = -32768, lastLogged = 0, moves = 0; Uint32 movedAt = 0; };
struct ButtonState { bool down = false; int presses = 0; Uint32 changedAt = 0; };
struct Dev {
    SDL_Joystick* js = nullptr; SDL_JoystickID id = -1; std::string name; char guid[40] = {}; int vid = 0, pid = 0;
    std::vector<AxisState> axes; std::vector<ButtonState> buttons; std::vector<int> hats; std::vector<int> hatSeen; std::vector<Uint32> hatAt;
};

static std::vector<Dev> g_devs;
static std::deque<std::string> g_log;
static FILE* g_logFile = nullptr;

static void logEvent(const std::string& s) {
    char stamp[24]; std::snprintf(stamp, sizeof stamp, "%7.2f ", SDL_GetTicks() / 1000.0);
    std::string line = std::string(stamp) + s;
    g_log.push_back(line); while (g_log.size() > 12) g_log.pop_front();
    if (g_logFile) { std::fprintf(g_logFile, "%s\n", line.c_str()); std::fflush(g_logFile); }
}

static void addDevice(int index) {
    SDL_Joystick* js = SDL_JoystickOpen(index);
    if (!js) return;
    Dev d; d.js = js; d.id = SDL_JoystickInstanceID(js);
    for (auto& e : g_devs) if (e.id == d.id) { SDL_JoystickClose(js); return; }
    const char* n = SDL_JoystickName(js); d.name = n ? n : "?";
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(js), d.guid, sizeof d.guid);
    d.vid = SDL_JoystickGetVendor(js); d.pid = SDL_JoystickGetProduct(js);
    d.axes.resize((size_t)SDL_JoystickNumAxes(js)); d.buttons.resize((size_t)SDL_JoystickNumButtons(js));
    d.hats.assign((size_t)SDL_JoystickNumHats(js), 0); d.hatSeen.assign(d.hats.size(), 0); d.hatAt.assign(d.hats.size(), 0);
    for (size_t i = 0; i < d.axes.size(); i++) { int v = SDL_JoystickGetAxis(js, (int)i); d.axes[i].rest = d.axes[i].cur = d.axes[i].lastLogged = v; }
    char b[160]; std::snprintf(b, sizeof b, "DEVICE %zu: %s %04X:%04X %zu AXES %zu BUTTONS %zu HATS", g_devs.size(), d.name.c_str(), d.vid, d.pid, d.axes.size(), d.buttons.size(), d.hats.size());
    logEvent(b);
    g_devs.push_back(std::move(d));
}

static void writeSummary() {
    FILE* f = std::fopen("logs/joytest_summary.txt", "w");
    if (!f) return;
    for (size_t di = 0; di < g_devs.size(); di++) {
        auto& d = g_devs[di];
        std::fprintf(f, "DEVICE %zu: %s  %04X:%04X  guid %s\n", di, d.name.c_str(), d.vid, d.pid, d.guid);
        for (size_t i = 0; i < d.axes.size(); i++) {
            auto& a = d.axes[i];
            std::fprintf(f, "  axis %zu: rest %d  min %d  max %d  moves %d  %s\n", i, a.rest, a.mn > a.mx ? a.rest : a.mn, a.mn > a.mx ? a.rest : a.mx, a.moves,
                         a.moves == 0 ? "(never moved)" : (a.rest <= -30000 ? "(rests at MIN: pedal-like, pressed = +)" : a.rest >= 30000 ? "(rests at MAX: pedal-like, pressed = -)" : "(rests near centre: stick / wheel-like)"));
        }
        for (size_t i = 0; i < d.buttons.size(); i++) std::fprintf(f, "  button %zu: pressed %d times\n", i, d.buttons[i].presses);
        for (size_t i = 0; i < d.hats.size(); i++) std::fprintf(f, "  hat %zu: last seen %d, values seen mask %d\n", i, d.hats[i], d.hatSeen[i]);
    }
    std::fclose(f);
}

// ---- guided mode (--guided): asks for one control at a time and records which axis / button responded ----
struct Step { const char* key; const char* prompt; bool axis; bool optional; int dev = -1, index = -1, rest = 0, dir = 0; bool done = false, skipped = false; };
static std::vector<Step> g_steps = {
    {"steering", "TURN THE WHEEL FULLY RIGHT AND HOLD", true, false},
    {"throttle", "PRESS THE THROTTLE PEDAL FULLY DOWN", true, false},
    {"brake", "PRESS THE BRAKE PEDAL FULLY DOWN", true, false},
    {"clutch", "PRESS THE CLUTCH PEDAL FULLY DOWN (SPACE = SKIP)", true, true},
    {"paddle_right", "PRESS THE RIGHT PADDLE (UP-SHIFT)", false, false},
    {"paddle_left", "PRESS THE LEFT PADDLE (DOWN-SHIFT)", false, false},
    {"fire", "PRESS THE BUTTON YOU WANT FOR FIRE", false, false},
    {"dock", "PRESS THE BUTTON YOU WANT FOR DOCK", false, false},
    {"warp", "PRESS THE BUTTON YOU WANT FOR WARP", false, false},
    {"orbit_lock", "PRESS THE BUTTON YOU WANT FOR ORBIT LOCK", false, false},
    {"menu", "PRESS THE BUTTON YOU WANT FOR THE GAME MENU (SPACE = SKIP)", false, true},
    {"camera", "PRESS THE BUTTON YOU WANT FOR CAMERA VIEW (SPACE = SKIP)", false, true},
    {"weapon_next", "PRESS THE BUTTON YOU WANT FOR NEXT WEAPON (CYCLES BLASTER / BEAM / MISSILES)", false, false},
    {"weapon_prev", "PRESS THE BUTTON YOU WANT FOR PREVIOUS WEAPON (SPACE = SKIP)", false, true},
    {"lock_target", "PRESS THE BUTTON YOU WANT FOR LOCK TARGET (MISSILES) (SPACE = SKIP)", false, true},
    {"lock_clear", "PRESS THE BUTTON YOU WANT FOR CLEAR LOCK (SPACE = SKIP)", false, true},
};
// extra steps only selectable with --steps=... (wheels have ONE weapon-switch button, not one per weapon)
static std::vector<Step> g_extraSteps = {
    {"weapon_1", "PRESS THE BUTTON YOU WANT FOR WEAPON 1 BLASTER (SPACE = SKIP)", false, true},
    {"weapon_2", "PRESS THE BUTTON YOU WANT FOR WEAPON 2 MINING BEAM (SPACE = SKIP)", false, true},
    {"weapon_3", "PRESS THE BUTTON YOU WANT FOR WEAPON 3 MISSILES (SPACE = SKIP)", false, true},
    {"look_reset", "PRESS THE BUTTON YOU WANT FOR ANYTHING ELSE, E.G. BRAKE ASSIST (SPACE = SKIP)", false, true},
};
static std::string g_outFile = "logs/joytest_guided.json";
static bool g_guided = false;
static int g_gDev = 0;
static size_t g_gStep = 0;
static std::vector<int> g_gBase;   // axis values at the start of the step
static bool g_gPending = false;    // a control was detected for the current step: ENTER confirms, BACKSPACE retries

static void guidedBegin() {
    g_gBase.clear();
    if (g_gDev < (int)g_devs.size()) for (auto& a : g_devs[(size_t)g_gDev].axes) g_gBase.push_back(a.cur);
}
static void guidedWrite() {
    FILE* f = std::fopen(g_outFile.c_str(), "w");
    if (!f || g_gDev >= (int)g_devs.size()) { if (f) std::fclose(f); return; }
    auto& d = g_devs[(size_t)g_gDev];
    std::fprintf(f, "{\n  \"device\": \"%s\", \"vid\": \"%04X\", \"pid\": \"%04X\", \"axes\": %zu, \"buttons\": %zu, \"hats\": %zu,\n  \"controls\": {\n", d.name.c_str(), d.vid, d.pid, d.axes.size(), d.buttons.size(), d.hats.size());
    bool first = true;
    for (auto& st : g_steps) {
        if (!st.done) continue;
        std::fprintf(f, "%s    \"%s\": {\"type\": \"%s\", \"index\": %d", first ? "" : ",\n", st.key, st.axis ? "axis" : "button", st.index);
        if (st.axis) std::fprintf(f, ", \"rest\": %d, \"pressed_direction\": %d", st.rest, st.dir);
        std::fprintf(f, "}"); first = false;
    }
    std::fprintf(f, "\n  },\n  \"skipped\": [");
    first = true; for (auto& st : g_steps) if (st.skipped) { std::fprintf(f, "%s\"%s\"", first ? "" : ", ", st.key); first = false; }
    std::fprintf(f, "]\n}\n"); std::fclose(f);
}
static void guidedAxis(int dev, int axis, int v) {
    if (!g_guided || dev != g_gDev || g_gStep >= g_steps.size() || !g_steps[g_gStep].axis) return;
    if (axis >= (int)g_gBase.size() || std::abs(v - g_gBase[(size_t)axis]) < 20000) return;
    for (auto& st : g_steps) if (st.done && st.axis && st.index == axis && std::string(st.key) != g_steps[g_gStep].key) return;   // already used by another control
    if (g_gPending) return;
    auto& st = g_steps[g_gStep]; st.dev = dev; st.index = axis; st.rest = g_gBase[(size_t)axis]; st.dir = v > st.rest ? 1 : -1; g_gPending = true;
    logEvent(std::string("GUIDED CANDIDATE ") + st.key + " = AXIS " + num(axis) + " REST " + num(st.rest) + " PRESSED " + (st.dir > 0 ? "+" : "-"));
}
static void guidedButton(int dev, int b) {
    if (!g_guided || dev != g_gDev || g_gStep >= g_steps.size() || g_steps[g_gStep].axis) return;
    for (auto& st : g_steps) if (st.done && !st.axis && st.index == b) return;   // already assigned
    if (g_gPending) return;
    auto& st = g_steps[g_gStep]; st.dev = dev; st.index = b; g_gPending = true;
    logEvent(std::string("GUIDED CANDIDATE ") + st.key + " = BUTTON " + num(b));
}
static void guidedConfirm() {
    if (!g_guided || !g_gPending || g_gStep >= g_steps.size()) return;
    auto& st = g_steps[g_gStep]; st.done = true; g_gPending = false;
    logEvent(std::string("GUIDED CONFIRMED ") + st.key); g_gStep++; guidedWrite();
}
static void guidedBack() {
    if (!g_guided) return;
    if (g_gPending) { g_gPending = false; logEvent("GUIDED RETRY"); return; }
    if (g_gStep == 0) return;
    g_gStep--; auto& st = g_steps[g_gStep]; st.done = false; st.skipped = false; st.index = -1; logEvent(std::string("GUIDED BACK TO ") + st.key); guidedWrite();
}
static void guidedSkip() {
    if (!g_guided || g_gPending || g_gStep >= g_steps.size() || !g_steps[g_gStep].optional) return;
    g_steps[g_gStep].skipped = true; logEvent(std::string("GUIDED ") + g_steps[g_gStep].key + " SKIPPED"); g_gStep++; guidedWrite();
}

static void drawGuided(Uint32 now) {
    SDL_SetRenderDrawColor(g_r, 14, 16, 20, 255); SDL_RenderClear(g_r);
    bool done = g_gStep >= g_steps.size();
    // header
    rect(0, 0, 1300, 118, 22, 30, 46);
    if (!done) {
        text(24, 12, "STEP " + num((int)g_gStep + 1) + " OF " + num((int)g_steps.size()), 2, 255, 220, 90);
        text(24, 40, g_steps[g_gStep].prompt, 3, 255, 255, 255);
        if (g_gPending) {
            auto& st = g_steps[g_gStep];
            text(24, 78, std::string("GOT ") + (st.axis ? "AXIS " : "BUTTON ") + num(st.index) + "  -  ENTER = OK   BACKSPACE = TRY AGAIN", 3, 120, 255, 140);
        } else text(24, 84, "MOVE OR PRESS THE CONTROL ONCE, THEN RELEASE.   SPACE = SKIP (IF OPTIONAL)   BACKSPACE = GO BACK", 2, 170, 180, 200);
    } else text(24, 48, "ALL DONE - SAVED TO LOGS/JOYTEST_GUIDED.JSON - PRESS ESC", 3, 120, 255, 140);
    // checklist
    text(24, 136, "CONTROLS", 2, 140, 180, 255);
    for (size_t i = 0; i < g_steps.size(); i++) {
        auto& st = g_steps[i]; int y = 164 + (int)i * 34; bool cur = i == g_gStep && !done;
        if (cur) rect(16, y - 6, 400, 30, 46, 54, 30);
        std::string label = std::string(st.key);
        for (auto& c : label) if (c == '_') c = ' ';
        std::string val = st.done ? (st.axis ? "AXIS " : "BUTTON ") + num(st.index) : st.skipped ? "SKIPPED" : (cur && g_gPending ? (st.axis ? "AXIS " : "BUTTON ") + num(st.index) + " ?" : "-");
        text(28, y, label, 2, cur ? 255 : (st.done ? 150 : 130), cur ? 230 : (st.done ? 255 : 130), cur ? 90 : (st.done ? 160 : 140));
        text(230, y, val, 2, st.done ? 120 : 150, st.done ? 255 : 150, st.done ? 140 : 160);
    }
    // live view of the device
    if (g_gDev >= (int)g_devs.size()) { text(460, 140, "DEVICE NOT FOUND", 3, 255, 120, 120); return; }
    auto& d = g_devs[(size_t)g_gDev];
    text(460, 136, "LIVE: " + d.name, 2, 140, 180, 255);
    for (size_t i = 0; i < d.axes.size(); i++) {
        auto& a = d.axes[i]; int y = 170 + (int)i * 40; bool hot = now - a.movedAt < 300;
        text(460, y + 6, "AXIS " + num((int)i), 2, hot ? 255 : 190, hot ? 230 : 190, hot ? 90 : 190);
        int bx = 590, bw = 460;
        rect(bx, y, bw, 28, 36, 40, 50); rect(bx, y, bw, 28, 84, 90, 106, false);
        int cx = bx + bw / 2, fx = bx + (int)((a.cur + 32768) / 65535.0f * bw);
        rect(std::min(cx, fx), y + 3, std::abs(fx - cx), 22, hot ? 255 : 80, hot ? 200 : 150, hot ? 60 : 210);
        rect(cx, y, 2, 28, 140, 140, 150);
        text(bx + bw + 14, y + 6, num(a.cur, true), 2, 225, 225, 225);
    }
    int by = 170 + (int)d.axes.size() * 40 + 24;
    text(460, by, "BUTTONS", 2, 140, 180, 255); by += 28;
    for (size_t i = 0; i < d.buttons.size(); i++) {
        auto& b = d.buttons[i]; int col = (int)(i % 12), row = (int)(i / 12), x = 460 + col * 68, y = by + row * 44;
        rect(x, y, 60, 36, b.down ? 40 : 34, b.down ? 210 : 38, b.down ? 80 : 46);
        rect(x, y, 60, 36, 90, 96, 110, false);
        text(x + (i < 10 ? 22 : 16), y + 11, num((int)i), 2, 240, 240, 240);
    }
    int hy = by + (int)((d.buttons.size() + 11) / 12) * 44 + 12;
    for (size_t i = 0; i < d.hats.size(); i++) {
        int v = d.hats[i], hx = 460 + (int)i * 130, cxh = hx + 40, cyh = hy + 46;
        text(hx, hy - 6, "HAT " + num((int)i), 2, 190, 190, 190);
        rect(cxh - 26, cyh - 26, 52, 52, 36, 40, 50); rect(cxh - 26, cyh - 26, 52, 52, 90, 96, 110, false);
        if (v & SDL_HAT_UP) rect(cxh - 8, cyh - 24, 16, 16, 90, 255, 120);
        if (v & SDL_HAT_DOWN) rect(cxh - 8, cyh + 8, 16, 16, 90, 255, 120);
        if (v & SDL_HAT_LEFT) rect(cxh - 24, cyh - 8, 16, 16, 90, 255, 120);
        if (v & SDL_HAT_RIGHT) rect(cxh + 8, cyh - 8, 16, 16, 90, 255, 120);
    }
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--guided")) g_guided = true;
        else if (!std::strncmp(argv[i], "--dev=", 6)) g_gDev = std::atoi(argv[i] + 6);
        else if (!std::strncmp(argv[i], "--out=", 6)) g_outFile = argv[i] + 6;
        else if (!std::strncmp(argv[i], "--steps=", 8)) {   // run only these steps (comma separated keys from either list)
            std::vector<Step> pick; std::string list = std::string(argv[i] + 8) + ",";
            for (size_t p = 0; (p = list.find(',')) != std::string::npos; list.erase(0, p + 1)) {
                std::string k = list.substr(0, p);
                for (auto& st : g_steps) if (k == st.key) pick.push_back(st);
                for (auto& st : g_extraSteps) if (k == st.key) pick.push_back(st);
            }
            if (!pick.empty()) g_steps = pick;
        }
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    if (std::system("mkdir -p logs") != 0) std::fprintf(stderr, "warning: could not create logs/\n");
    g_logFile = std::fopen("logs/joytest.log", "w");
    SDL_Window* win = SDL_CreateWindow("JOYTEST - move / press one control at a time (ESC quits, R resets)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1300, 720, SDL_WINDOW_SHOWN);
    if (g_guided) SDL_SetWindowTitle(win, "JOYTEST GUIDED - follow the prompt at the top, one control at a time");
    if (!win) { std::fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    g_r = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_r) g_r = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    for (int i = 0; i < SDL_NumJoysticks(); i++) addDevice(i);
    if (g_devs.empty()) logEvent("NO JOYSTICK DEVICES FOUND (PLUG IN AND THEY APPEAR HERE)");
    if (g_guided) guidedBegin();

    bool quit = false;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_q) quit = true;
                else if (e.key.keysym.sym == SDLK_r) { for (auto& d : g_devs) for (auto& a : d.axes) { a.mn = 32767; a.mx = -32768; a.moves = 0; a.rest = a.cur; a.lastLogged = a.cur; } logEvent("RESET MIN/MAX"); }
                else if (e.key.keysym.sym == SDLK_SPACE) { guidedSkip(); guidedBegin(); }
                else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) { guidedConfirm(); guidedBegin(); }
                else if (e.key.keysym.sym == SDLK_BACKSPACE) { guidedBack(); guidedBegin(); }
                else if (e.key.keysym.sym == SDLK_s) { writeSummary(); logEvent("SUMMARY SAVED TO LOGS/JOYTEST_SUMMARY.TXT"); }
            } else if (e.type == SDL_JOYDEVICEADDED) addDevice(e.jdevice.which);
            else if (e.type == SDL_JOYDEVICEREMOVED) {
                for (size_t i = 0; i < g_devs.size(); i++) if (g_devs[i].id == e.jdevice.which) { logEvent("DEVICE REMOVED: " + g_devs[i].name); SDL_JoystickClose(g_devs[i].js); g_devs.erase(g_devs.begin() + (long)i); break; }
            } else if (e.type == SDL_JOYAXISMOTION) {
                for (size_t di = 0; di < g_devs.size(); di++) if (g_devs[di].id == e.jaxis.which && e.jaxis.axis < g_devs[di].axes.size()) {
                    auto& a = g_devs[di].axes[e.jaxis.axis]; int v = e.jaxis.value;
                    a.cur = v; a.mn = std::min(a.mn, v); a.mx = std::max(a.mx, v); a.movedAt = SDL_GetTicks();
                    guidedAxis((int)di, e.jaxis.axis, v);
                    if (std::abs(v - a.lastLogged) > 2500) { a.lastLogged = v; a.moves++; logEvent("DEV " + num((int)di) + " AXIS " + num(e.jaxis.axis) + " = " + num(v, true)); }
                }
            } else if (e.type == SDL_JOYBUTTONDOWN || e.type == SDL_JOYBUTTONUP) {
                for (size_t di = 0; di < g_devs.size(); di++) if (g_devs[di].id == e.jbutton.which && e.jbutton.button < g_devs[di].buttons.size()) {
                    auto& b = g_devs[di].buttons[e.jbutton.button]; bool dn = e.type == SDL_JOYBUTTONDOWN;
                    b.down = dn; b.changedAt = SDL_GetTicks(); if (dn) { b.presses++; guidedButton((int)di, e.jbutton.button); }
                    logEvent("DEV " + num((int)di) + " BUTTON " + num(e.jbutton.button) + (dn ? " DOWN" : " UP"));
                }
            } else if (e.type == SDL_JOYHATMOTION) {
                for (size_t di = 0; di < g_devs.size(); di++) if (g_devs[di].id == e.jhat.which && e.jhat.hat < g_devs[di].hats.size()) {
                    g_devs[di].hats[e.jhat.hat] = e.jhat.value; g_devs[di].hatSeen[e.jhat.hat] |= e.jhat.value; g_devs[di].hatAt[e.jhat.hat] = SDL_GetTicks();
                    logEvent("DEV " + num((int)di) + " HAT " + num(e.jhat.hat) + " = " + num(e.jhat.value));
                }
            }
        }
        Uint32 now0 = SDL_GetTicks();
        { static size_t lastStep = 999; if (g_guided && g_gStep != lastStep) { lastStep = g_gStep; guidedBegin(); } }
        if (g_guided) { drawGuided(now0); SDL_RenderPresent(g_r); continue; }
        // draw
        SDL_SetRenderDrawColor(g_r, 14, 16, 20, 255); SDL_RenderClear(g_r);
        Uint32 now = SDL_GetTicks();
        int panelW = g_devs.size() <= 1 ? 1260 : 630;
        for (size_t di = 0; di < g_devs.size(); di++) {
            auto& d = g_devs[di]; int px = 20 + (int)di * 640, py = 12;
            text(px, py, "DEV " + num((int)di) + ": " + d.name + "  " + [&]{ char b[16]; std::snprintf(b, sizeof b, "%04X:%04X", d.vid, d.pid); return std::string(b); }(), 2, 120, 220, 255);
            py += 26;
            for (size_t i = 0; i < d.axes.size(); i++) {
                auto& a = d.axes[i]; int y = py + (int)i * 30; bool hot = now - a.movedAt < 400;
                text(px, y + 4, "AXIS " + num((int)i), 2, hot ? 255 : 200, hot ? 230 : 200, hot ? 80 : 200);
                int bx = px + 90, bw = 300;
                rect(bx, y, bw, 22, 40, 44, 52); rect(bx, y, bw, 22, 90, 96, 110, false);
                float f = (a.cur + 32768) / 65535.0f;
                int cx = bx + bw / 2, fx = bx + (int)(f * bw);
                rect(std::min(cx, fx), y + 2, std::abs(fx - cx), 18, hot ? 255 : 90, hot ? 200 : 170, hot ? 60 : 220);
                rect(cx, y, 1, 22, 150, 150, 150);
                if (a.mn <= a.mx) { rect(bx + (int)((a.mn + 32768) / 65535.0f * bw), y, 2, 22, 255, 90, 90); rect(bx + (int)((a.mx + 32768) / 65535.0f * bw), y, 2, 22, 90, 255, 90); }
                rect(bx + (int)((a.rest + 32768) / 65535.0f * bw) - 1, y - 3, 3, 4, 255, 255, 0);
                text(bx + bw + 10, y + 4, num(a.cur, true), 2, 230, 230, 230);
                text(bx + bw + 100, y + 4, "REST " + num(a.rest), 1, 200, 200, 90);
                text(bx + bw + 100, y + 14, "MIN " + num(a.mn > a.mx ? a.rest : a.mn) + " MAX " + num(a.mn > a.mx ? a.rest : a.mx), 1, 160, 160, 170);
            }
            int by = py + (int)d.axes.size() * 30 + 14;
            text(px, by, "BUTTONS (GREEN = DOWN, BLUE = PRESSED BEFORE)", 1, 170, 170, 180); by += 14;
            for (size_t i = 0; i < d.buttons.size(); i++) {
                auto& b = d.buttons[i]; int col = (int)(i % 8), row = (int)(i / 8); int x = px + col * 76, y = by + row * 34;
                bool flash = now - b.changedAt < 250;
                rect(x, y, 70, 28, b.down ? 40 : (b.presses ? 30 : 34), b.down ? 200 : (b.presses ? 70 : 38), b.down ? 70 : (b.presses ? 140 : 46));
                rect(x, y, 70, 28, flash ? 255 : 100, flash ? 255 : 106, flash ? 120 : 120, false);
                text(x + 6, y + 8, "B" + num((int)i) + (b.presses ? " X" + num(b.presses) : ""), 1, 240, 240, 240);
            }
            int hy = by + (int)((d.buttons.size() + 7) / 8) * 34 + 8;
            for (size_t i = 0; i < d.hats.size(); i++) {
                int hx = px + (int)i * 120; int v = d.hats[i]; bool hot = now - d.hatAt[i] < 400;
                text(hx, hy, "HAT " + num((int)i) + " = " + num(v), 1, 200, 200, 200);
                int cxh = hx + 40, cyh = hy + 50;
                rect(cxh - 28, cyh - 28, 56, 56, 40, 44, 52); rect(cxh - 28, cyh - 28, 56, 56, 100, 106, 120, false);
                if (v & SDL_HAT_UP) rect(cxh - 8, cyh - 26, 16, 16, 90, 255, 120);
                if (v & SDL_HAT_DOWN) rect(cxh - 8, cyh + 10, 16, 16, 90, 255, 120);
                if (v & SDL_HAT_LEFT) rect(cxh - 26, cyh - 8, 16, 16, 90, 255, 120);
                if (v & SDL_HAT_RIGHT) rect(cxh + 10, cyh - 8, 16, 16, 90, 255, 120);
                if (hot) rect(cxh - 3, cyh - 3, 6, 6, 255, 230, 80);
            }
            (void)panelW;
        }
        int ly = g_guided ? 380 : 470;
        text(20, ly - 18, "EVENT LOG (ALSO IN LOGS/JOYTEST.LOG)   R = RESET MIN/MAX   S = SAVE SUMMARY   ESC = QUIT", 1, 170, 170, 180);
        int line = 0; for (auto& s : g_log) { text(20, ly + line * 16, s, 2, 200, 235, 200); line++; }
        if (g_guided) {
            rect(0, 596, 1300, 124, 24, 30, 44);
            if (g_gStep < g_steps.size()) {
                text(20, 604, "STEP " + num((int)g_gStep + 1) + " OF " + num((int)g_steps.size()) + ":", 2, 255, 220, 90);
                text(20, 628, g_steps[g_gStep].prompt, 3, 255, 255, 255);
                text(20, 660, "DEVICE " + num(g_gDev) + "  (RELEASE THE CONTROL BEFORE THE NEXT STEP)", 1, 170, 170, 180);
            } else text(20, 620, "ALL DONE - RESULT SAVED TO LOGS/JOYTEST_GUIDED.JSON - PRESS ESC", 3, 90, 255, 120);
            int x = 700, y = 604; for (size_t i = 0; i < g_steps.size(); i++) {
                auto& st = g_steps[i]; std::string t = std::string(st.key) + (st.done ? (st.axis ? " = AXIS " : " = BTN ") + num(st.index) : st.skipped ? " SKIPPED" : "");
                text(x + (int)(i / 7) * 300, y + (int)(i % 7) * 14, t, 1, st.done ? 120 : 130, st.done ? 255 : 130, st.done ? 140 : 140);
            }
        }
        SDL_RenderPresent(g_r);
    }
    writeSummary();
    if (g_logFile) std::fclose(g_logFile);
    for (auto& d : g_devs) SDL_JoystickClose(d.js);
    SDL_Quit();
    return 0;
}
