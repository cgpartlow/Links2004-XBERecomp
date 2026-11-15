// pc_controls.h: the bindings, the keyboard and mouse as controller 1, and the mouse swing.
//
// Keys are read with GetAsyncKeyState when the game's window is the foreground one, from whichever thread polls the
// controller, so nothing depends on the window's messages.
//
// The mouse swing: the left button going down inside the window starts a drag. The cursor is put back where it was
// each time the controller is polled, and how far it had moved is added up, so the drag never runs into the edge of
// the screen. The sum, over the travel a full swing takes (a share of the window's height, less with more
// sensitivity), is the left thumbstick: back (down) is the backswing, forward (up) the downswing, and sideways is the
// stick's sideways, the game's draw and fade, with a dead band so a straight swing need not be perfectly straight.
#include "common/pc_controls.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include <toml++/toml.hpp>

#include "app/app.h"
#include "core/log.h"

namespace links {

bool g_keyboard_mouse = true;
uint32_t g_mouse_sensitivity = 5;

namespace {

// what a binding presses, in kDefaults' order
enum Action {
    kDpadUp, kDpadDown, kDpadLeft, kDpadRight, kButtonA, kButtonB, kButtonX, kButtonY, kLeftTrigger, kRightTrigger,
    kWhite, kBlack, kStart, kBack, kLeftThumb, kRightThumb, kSpinUp, kSpinDown, kSpinLeft, kSpinRight, kQuickMenu,
    kActions
};

const Binding kDefaults[kActions] = {
    {"dpad_up", "D-PAD UP", "MENU UP / AIM MARKER", {VK_UP, 'W'}},
    {"dpad_down", "D-PAD DOWN", "MENU DOWN / AIM MARKER", {VK_DOWN, 'S'}},
    {"dpad_left", "D-PAD LEFT", "MENU LEFT / AIM MARKER", {VK_LEFT, 'A'}},
    {"dpad_right", "D-PAD RIGHT", "MENU RIGHT / AIM MARKER", {VK_RIGHT, 'D'}},
    {"a", "A", "SELECT / AIM MARKER VIEW", {VK_RETURN, VK_SPACE}},
    {"b", "B", "BACK / SHOT TYPE / MULLIGAN", {VK_BACK, VK_RBUTTON}},
    {"x", "X", "TOP VIEW / REPLAY", {'F', 0}},
    {"y", "Y", "Y BUTTON", {'R', 0}},
    {"left_trigger", "LEFT TRIGGER", "CHANGE CLUB", {'Q', 0}},
    {"right_trigger", "RIGHT TRIGGER", "CHANGE CLUB", {'E', 0}},
    {"white", "WHITE", "WHITE BUTTON", {'Z', 0}},
    {"black", "BLACK", "BLACK BUTTON", {'C', 0}},
    {"start", "START", "PAUSE", {VK_ESCAPE, 'P'}},
    {"back", "BACK", "BACK BUTTON", {VK_TAB, 0}},
    {"left_thumb", "LEFT STICK CLICK", "PUTTING GRID", {'G', 0}},
    {"right_thumb", "RIGHT STICK CLICK", "RIGHT STICK CLICK", {'V', 0}},
    {"spin_up", "RIGHT STICK UP", "BALL SPIN", {'I', 0}},
    {"spin_down", "RIGHT STICK DOWN", "BALL SPIN", {'K', 0}},
    {"spin_left", "RIGHT STICK LEFT", "BALL SPIN", {'J', 0}},
    {"spin_right", "RIGHT STICK RIGHT", "BALL SPIN", {'L', 0}},
    {"quick_menu", "BACK + START", "QUICK MENU", {VK_F1, 0}},
};

std::mutex g_lock;
std::atomic<uint32_t> g_version{1};
Binding g_bindings[kActions];
bool g_bindings_set = false;

// under g_lock
Binding* Table() {
    if (!g_bindings_set) {
        std::copy(std::begin(kDefaults), std::end(kDefaults), g_bindings);
        g_bindings_set = true;
    }
    return g_bindings;
}

// ---- key names ----

struct Named {
    int key;
    const char* name;
};
const Named kNames[] = {
    {VK_RETURN, "Enter"}, {VK_SPACE, "Space"}, {VK_BACK, "Backspace"}, {VK_TAB, "Tab"}, {VK_ESCAPE, "Esc"},
    {VK_UP, "Up"}, {VK_DOWN, "Down"}, {VK_LEFT, "Left"}, {VK_RIGHT, "Right"},
    {VK_LSHIFT, "Left Shift"}, {VK_RSHIFT, "Right Shift"}, {VK_LCONTROL, "Left Ctrl"}, {VK_RCONTROL, "Right Ctrl"},
    {VK_LMENU, "Left Alt"}, {VK_RMENU, "Right Alt"}, {VK_INSERT, "Insert"}, {VK_DELETE, "Delete"},
    {VK_HOME, "Home"}, {VK_END, "End"}, {VK_PRIOR, "Page Up"}, {VK_NEXT, "Page Down"}, {VK_CAPITAL, "Caps Lock"},
    {VK_RBUTTON, "Mouse Right"}, {VK_MBUTTON, "Mouse Middle"}, {VK_XBUTTON1, "Mouse 4"}, {VK_XBUTTON2, "Mouse 5"},
    {VK_OEM_1, ";"}, {VK_OEM_PLUS, "="}, {VK_OEM_COMMA, ","}, {VK_OEM_MINUS, "-"}, {VK_OEM_PERIOD, "."},
    {VK_OEM_2, "/"}, {VK_OEM_3, "`"}, {VK_OEM_4, "["}, {VK_OEM_5, "\\"}, {VK_OEM_6, "]"}, {VK_OEM_7, "'"},
    {VK_MULTIPLY, "Num *"}, {VK_ADD, "Num +"}, {VK_SUBTRACT, "Num -"}, {VK_DECIMAL, "Num ."}, {VK_DIVIDE, "Num /"},
};

std::string Lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// a name from the settings file back into its key, 0 when it is none
int KeyFromName(const std::string& text) {
    std::string name = Lower(text);
    if (name.empty()) return 0;
    for (const Named& n : kNames)
        if (Lower(n.name) == name) return n.key;
    if (name.size() == 1 && ((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= '0' && name[0] <= '9')))
        return std::toupper(static_cast<unsigned char>(name[0]));
    if (name[0] == 'f' && name.size() <= 3) {
        int n = std::atoi(name.c_str() + 1);
        if (n >= 1 && n <= 24) return VK_F1 + n - 1;
    }
    if (name.rfind("num ", 0) == 0 && name.size() == 5 && name[4] >= '0' && name[4] <= '9')
        return VK_NUMPAD0 + (name[4] - '0');
    if (name.rfind("key ", 0) == 0) {
        int k = int(std::strtol(name.c_str() + 4, nullptr, 0));
        if (k > 0 && k < 255) return k;
    }
    return 0;
}

// ---- reading ----

bool Foreground() { return GetForegroundWindow() == static_cast<HWND>(app::Window()); }
bool Down(int key) { return key && (GetAsyncKeyState(key) & 0x8000) != 0; }

// keys a binding can hold: not the left mouse button (the swing), not the either-side Shift, Ctrl and Alt (their left
// and right ones are told apart), not the keys Windows keeps
bool Bindable(int key) {
    if (key < VK_RBUTTON || key > 0xFE) return false;
    if (key == VK_CANCEL || key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU) return false;
    if (key == VK_LWIN || key == VK_RWIN || key == VK_APPS || key == VK_SNAPSHOT) return false;
    if (key == VK_NUMLOCK || key == VK_SCROLL || key == VK_PAUSE) return false;
    return true;
}

// ---- the mouse swing, under g_lock ----

struct Swing {
    bool drag = false, was_down = false;
    POINT anchor{};
    float x = 0, y = 0;  // the mouse's travel since the button went down, down and right positive, within the range
};
Swing g_swing;
std::atomic<bool> g_swinging{false};

void MouseSwing(input::Pad& pad, bool allowed, bool foreground) {
    HWND hwnd = static_cast<HWND>(app::Window());
    Swing& s = g_swing;
    bool down = foreground && Down(VK_LBUTTON);
    RECT client{};
    GetClientRect(hwnd, &client);
    if (!s.drag && down && !s.was_down && allowed) {
        POINT at{};
        GetCursorPos(&at);
        POINT in = at;
        ScreenToClient(hwnd, &in);
        if (PtInRect(&client, in)) {
            s.drag = true;
            s.anchor = at;
            s.x = s.y = 0;
        }
    } else if (s.drag && (!down || !allowed)) {
        s.drag = false;
    }
    s.was_down = down;
    g_swinging = s.drag;
    if (!s.drag) return;
    // a full swing's travel: the window's height at sensitivity 1, a quarter of it at 4 and an eighth at 10
    float sens = float(std::clamp(g_mouse_sensitivity, 1u, 10u));
    float range = std::max(40.0f, float(client.bottom - client.top) * 1.25f / sens);
    POINT at{};
    GetCursorPos(&at);
    s.x = std::clamp(s.x + float(at.x - s.anchor.x), -range * 2, range * 2);
    s.y = std::clamp(s.y + float(at.y - s.anchor.y), -range, range);
    if (at.x != s.anchor.x || at.y != s.anchor.y) SetCursorPos(s.anchor.x, s.anchor.y);
    // sideways takes twice the travel, past a dead band of an eighth of it
    constexpr float kDead = 0.125f;
    float side = s.x / (range * 2);
    side = std::fabs(side) <= kDead ? 0.0f : (side - std::copysign(kDead, side)) / (1 - kDead);
    pad.lx = int16_t(std::lround(side * 32767.0f));
    pad.ly = int16_t(std::lround(-s.y / range * 32767.0f));  // the stick's up is positive
}

}  // namespace

std::vector<Binding> Bindings() {
    std::lock_guard<std::mutex> l(g_lock);
    Binding* t = Table();
    return std::vector<Binding>(t, t + kActions);
}

void SetBinding(size_t action, int slot, int key) {
    if (action >= kActions || slot < 0 || slot > 1) return;
    std::lock_guard<std::mutex> l(g_lock);
    Binding* t = Table();
    if (key) {
        for (int a = 0; a < kActions; a++)
            for (int& k : t[a].key)
                if (k == key) k = 0;
    }
    t[action].key[slot] = key;
    g_version++;
}

void ResetBindings() {
    std::lock_guard<std::mutex> l(g_lock);
    std::copy(std::begin(kDefaults), std::end(kDefaults), g_bindings);
    g_bindings_set = true;
    g_version++;
}

uint32_t BindingsVersion() { return g_version; }

std::string PromptKey(const char* id) {
    int key = 0;
    {
        std::lock_guard<std::mutex> l(g_lock);
        const Binding* t = Table();
        for (int a = 0; a < kActions && !key; a++)
            if (!std::strcmp(t[a].id, id)) key = t[a].key[0] ? t[a].key[0] : t[a].key[1];
    }
    // the menus' font has the four arrows as characters 0x8B to 0x8E: down, up, left, right
    switch (key) {
        case 0: return {};
        case VK_UP: return "\x8C";
        case VK_DOWN: return "\x8B";
        case VK_LEFT: return "\x8D";
        case VK_RIGHT: return "\x8E";
        case VK_RETURN: return "ENT";
        case VK_SPACE: return "SPC";
        case VK_BACK: return "BKS";
        case VK_TAB: return "TAB";
        case VK_ESCAPE: return "ESC";
        case VK_RBUTTON: return "RMB";
        case VK_MBUTTON: return "MMB";
        case VK_XBUTTON1: return "M4";
        case VK_XBUTTON2: return "M5";
        case VK_LSHIFT: return "LSH";
        case VK_RSHIFT: return "RSH";
        case VK_LCONTROL: return "LCT";
        case VK_RCONTROL: return "RCT";
        case VK_LMENU: return "LAL";
        case VK_RMENU: return "RAL";
        case VK_INSERT: return "INS";
        case VK_DELETE: return "DEL";
        case VK_HOME: return "HOM";
        case VK_END: return "END";
        case VK_PRIOR: return "PGU";
        case VK_NEXT: return "PGD";
        case VK_CAPITAL: return "CAP";
        default: break;
    }
    char b[8];
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) {
        std::snprintf(b, sizeof b, "N%d", key - VK_NUMPAD0);
        return b;
    }
    // letters, digits, F keys and punctuation are short as they are; anything else is its code
    std::string name = KeyName(key);
    if (name.rfind("Num ", 0) == 0) name = "N" + name.substr(4);
    if (name.size() > 3) {
        std::snprintf(b, sizeof b, "K%02X", key);
        name = b;
    }
    return name;
}

std::string KeyName(int key) {
    if (!key) return {};
    for (const Named& n : kNames)
        if (n.key == key) return n.name;
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) return std::string(1, char(key));
    char b[16];
    if (key >= VK_F1 && key <= VK_F24) std::snprintf(b, sizeof b, "F%d", key - VK_F1 + 1);
    else if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) std::snprintf(b, sizeof b, "Num %d", key - VK_NUMPAD0);
    else std::snprintf(b, sizeof b, "Key 0x%02X", key);
    return b;
}

void LoadControls(const void* table) {
    const toml::table& t = *static_cast<const toml::table*>(table);
    g_keyboard_mouse = t["keyboard_mouse"].value_or(g_keyboard_mouse);
    g_mouse_sensitivity = std::clamp(t["mouse_swing_sensitivity"].value_or(g_mouse_sensitivity), 1u, 10u);
    const toml::table* keys = t["keys"].as_table();
    if (!keys) return;
    std::lock_guard<std::mutex> l(g_lock);
    Binding* b = Table();
    // an action the file lists has the file's keys, one name or a list of up to two; a key listed twice stays with
    // the first action that has it
    for (int a = 0; a < kActions; a++) {
        const toml::node* n = keys->get(b[a].id);
        if (!n) continue;
        int found[2] = {0, 0};
        if (const toml::array* list = n->as_array()) {
            for (size_t i = 0; i < list->size() && i < 2; i++) found[i] = KeyFromName(list->get(i)->value_or(std::string()));
        } else {
            found[0] = KeyFromName(n->value_or(std::string()));
        }
        for (int i = 0; i < 2; i++) b[a].key[i] = Bindable(found[i]) ? found[i] : 0;
    }
    for (int a = 0; a < kActions; a++)
        for (int i = 0; i < 2; i++)
            for (int o = 0; o < a * 2 + i; o++)
                if (b[a].key[i] && b[o / 2].key[o % 2] == b[a].key[i]) b[a].key[i] = 0;
    g_version++;
}

void SaveControls(void* table) {
    toml::table& t = *static_cast<toml::table*>(table);
    t.insert_or_assign("keyboard_mouse", g_keyboard_mouse);
    t.insert_or_assign("mouse_swing_sensitivity", int64_t(g_mouse_sensitivity));
    toml::table keys;
    std::lock_guard<std::mutex> l(g_lock);
    Binding* b = Table();
    for (int a = 0; a < kActions; a++) {
        toml::array list;
        for (int k : b[a].key)
            if (k) list.push_back(KeyName(k));
        keys.insert_or_assign(b[a].id, std::move(list));
    }
    t.insert_or_assign("keys", std::move(keys));
}

namespace {
// a key that types a character, or Backspace and Shift, which typing uses too; not Enter, which stays A
bool TypingKey(int key) {
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) return true;
    if (key == VK_SPACE || key == VK_BACK || key == VK_LSHIFT || key == VK_RSHIFT) return true;
    if (key >= VK_NUMPAD0 && key <= VK_DIVIDE) return true;
    return (key >= VK_OEM_1 && key <= VK_OEM_3) || (key >= VK_OEM_4 && key <= VK_OEM_8);
}
}  // namespace

void KeyboardMousePad(input::Pad& pad, bool swing, bool typing) {
    if (!g_keyboard_mouse) {
        g_swinging = false;
        return;
    }
    pad.connected = true;
    bool foreground = Foreground();
    std::lock_guard<std::mutex> l(g_lock);
    MouseSwing(pad, swing, foreground);
    if (!foreground) return;
    const Binding* b = Table();
    // While typing, the keys that type are not buttons, and Esc is B whatever B's key is (Backspace, usually, which
    // is typing's own). After typing ends they all stay out until they are let go: the Esc that left the name entry
    // is not also the next menu's.
    static bool typed = false;
    bool out = typing;
    if (typing) {
        typed = true;
    } else if (typed) {
        for (int k = VK_BACK; k <= 0xFE && !out; k++) out = (TypingKey(k) || k == VK_ESCAPE) && Down(k);
        typed = out;
    }
    auto down = [&](int key) { return Down(key) && !(out && (TypingKey(key) || key == VK_ESCAPE)); };
    if (typing && Down(VK_ESCAPE)) pad.analog[1] = 0xFF;
    auto held = [&](int action) { return down(b[action].key[0]) || down(b[action].key[1]); };
    // the report's buttons: D-pad up, down, left, right, START, BACK, the thumbstick clicks
    constexpr int kButtons[8] = {kDpadUp, kDpadDown, kDpadLeft, kDpadRight, kStart, kBack, kLeftThumb, kRightThumb};
    for (int i = 0; i < 8; i++)
        if (held(kButtons[i])) pad.buttons |= uint16_t(1u << i);
    // its analog buttons: A B X Y black white, the triggers
    constexpr int kAnalog[8] = {kButtonA, kButtonB, kButtonX, kButtonY, kBlack, kWhite, kLeftTrigger, kRightTrigger};
    for (int i = 0; i < 8; i++)
        if (held(kAnalog[i])) pad.analog[i] = 0xFF;
    int rx = int(held(kSpinRight)) - int(held(kSpinLeft)), ry = int(held(kSpinUp)) - int(held(kSpinDown));
    if (rx) pad.rx = int16_t(rx * 32767);
    if (ry) pad.ry = int16_t(ry * 32767);
}

bool MouseSwinging() { return g_swinging; }

bool QuickMenuKeyPressed() {
    static bool was = false;
    int keys[2];
    {
        std::lock_guard<std::mutex> l(g_lock);
        std::copy(std::begin(Table()[kQuickMenu].key), std::end(Table()[kQuickMenu].key), keys);
    }
    bool now = Foreground() && (Down(keys[0]) || Down(keys[1]));
    bool pressed = now && !was;
    was = now;
    return pressed;
}

bool AnyKeyDown() {
    if (!Foreground()) return false;
    for (int k = VK_RBUTTON; k <= 0xFE; k++)
        if (Bindable(k) && Down(k)) return true;
    return false;
}

namespace {
bool g_capture_armed = false;  // every key has been let go since BeginCapture
}

void BeginCapture() { g_capture_armed = false; }

int CaptureKey() {
    if (!Foreground()) return 0;
    if (!g_capture_armed) {
        g_capture_armed = !AnyKeyDown();
        return 0;
    }
    for (int k = VK_RBUTTON; k <= 0xFE; k++)
        if (Bindable(k) && Down(k)) return k;
    return 0;
}

}  // namespace links
