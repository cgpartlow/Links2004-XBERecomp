// quick_menu.h: the menu's state, input and drawing.
//
// The menu is drawn by hand on ImGui's foreground list, in the game's menu colours: a dark blue panel between light
// rails, white capitals, and the selected line on an orange band with yellow text. A page is a list of items: a
// setting (a label and its value, stepped with Left and Right), a key binding (the controller's button, what it does,
// and its two keys), or an action that asks first.
//
// Input comes three ways, all doing the same things. Controllers are read as the game polls them (PcPad), into a
// mask the menu compares frame to frame; the keyboard and mouse come from ImGui. While the menu is open the game's
// controllers read as untouched, and after it closes they stay so until everything has been let go, so the press that
// closed the menu is not also the game's.
#include "common/quick_menu.h"

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
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "app/app.h"
#include "common/direct_connect.h"
#include "common/menu_mouse.h"
#include "common/pc_controls.h"
#include "common/ui_font.h"
#include "core/log.h"
#include "imgui.h"

namespace links {

namespace {

using Clock = std::chrono::steady_clock;

// ---- the controllers ----

// a pad in one mask: D-pad (the left stick counts) up, down, left, right, START, BACK, A B X Y, then the previous and
// next page (white or the left trigger, black or the right one) and anything else held
constexpr uint32_t kUp = 1, kDown = 2, kLeft = 4, kRight = 8, kStart = 0x10, kBack = 0x20;
constexpr uint32_t kA = 0x100, kB = 0x200, kX = 0x400, kY = 0x800, kPrev = 0x1000, kNext = 0x2000, kOther = 0x4000;
constexpr int kStick = 20000;

uint32_t Mask(const input::Pad& p) {
    uint32_t m = p.buttons & 0x3F;
    if (p.buttons & 0xC0) m |= kOther;
    for (int i = 0; i < 4; i++)
        if (p.analog[i] > 0x40) m |= kA << i;
    if (p.analog[5] > 0x40 || p.analog[6] > 0x40) m |= kPrev;
    if (p.analog[4] > 0x40 || p.analog[7] > 0x40) m |= kNext;
    if (p.ly > kStick) m |= kUp;
    if (p.ly < -kStick) m |= kDown;
    if (p.lx < -kStick) m |= kLeft;
    if (p.lx > kStick) m |= kRight;
    if (std::abs(int(p.rx)) > kStick || std::abs(int(p.ry)) > kStick) m |= kOther;
    return m;
}

void Empty(input::Pad& p) {
    p.buttons = 0;
    std::fill(std::begin(p.analog), std::end(p.analog), uint8_t(0));
    p.lx = p.ly = p.rx = p.ry = 0;
}

std::atomic<uint32_t> g_pads[4];    // each controller as last polled, without the keyboard
std::atomic<uint32_t> g_game[4];    // each port as the game would get it, the keyboard and mouse in
std::atomic<bool> g_chord[4];       // BACK + START was held at the last poll
std::atomic<bool> g_toggle{false};  // BACK + START went down: the menu opens or closes at the next frame
std::atomic<bool> g_open{false};
std::atomic<bool> g_plugged[4];              // a controller is in the port
std::atomic<bool> g_keyboard_prompts{false};  // the keyboard or mouse was used after any controller
std::atomic<bool> g_release{false};  // closed, the game gets nothing until everything is let go
std::atomic<int64_t> g_last_held{0};  // when anything was last held during that wait, in ms
std::atomic<int64_t> g_release_start{0};

int64_t NowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(); }

uint32_t PadsHeld() {
    uint32_t m = 0;
    for (auto& p : g_pads) m |= p.load();
    return m;
}

// ---- the menu's state: the render thread's, under the UI lock ----

enum class Kind { Setting, Header, Bind, Action };
struct Item {
    Kind kind;
    std::string label, value, button;  // a binding's label is what it does, its button the controller's
    int index = 0;                     // the page's row, or the binding
    std::string keys[2];
};

constexpr int kControlsPage = 3;
constexpr int kPages = 4;
const char* const kTitles[kPages] = {"DISPLAY", "GRAPHICS", "EXPERIMENTAL", "CONTROLS"};

int g_page = 0;
int g_row[kPages] = {};     // each page's selected item
int g_scroll[kPages] = {};  // and its first shown one
int g_slot = 0;             // a binding's selected key, 0 or 1
bool g_capture = false;     // waiting for the key a binding gets
bool g_keys_up = false;     // the keyboard does nothing until every key is let go
bool g_click_used = false;  // this frame's click cancelled a binding: it is not also a click on the menu
struct Confirm {
    bool open = false;
    std::string text;
    void (*run)() = nullptr;
};
Confirm g_confirm;
uint32_t g_prev = ~0u;
Clock::time_point g_repeat_at;
uint32_t g_repeat = 0;
bool g_controller_last = false;  // the footer names the controller's buttons, else the keys

std::vector<Item> Items(int page) {
    std::vector<Item> out;
    if (page != kControlsPage) {
        const std::vector<QuickPage>& pages = QuickPages();
        if (size_t(page) >= pages.size()) return out;
        int i = 0;
        for (const QuickRow& r : pages[size_t(page)].rows) {
            Item it{r.confirm ? Kind::Action : Kind::Setting, r.label, r.value ? r.value() : std::string(), {}, i++, {}};
            out.push_back(std::move(it));
        }
        return out;
    }
    out.push_back({Kind::Setting, "KEYBOARD AND MOUSE", g_keyboard_mouse ? "ON" : "OFF", {}, 0, {}});
    out.push_back({Kind::Setting, "MOUSE SWING SENSITIVITY", std::to_string(g_mouse_sensitivity), {}, 1, {}});
    out.push_back({Kind::Header, "ACTION", {}, "CONTROLLER", 0, {"KEY", "ALTERNATE"}});
    std::vector<Binding> binds = Bindings();
    for (size_t b = 0; b < binds.size(); b++) {
        Item it{Kind::Bind, binds[b].does, {}, binds[b].button, int(b), {}};
        for (int k = 0; k < 2; k++) it.keys[k] = KeyName(binds[b].key[k]);
        out.push_back(std::move(it));
    }
    out.push_back({Kind::Action, "RESET KEYS TO DEFAULT", {}, {}, 0, {}});
    return out;
}

void Open() {
    g_prev = ~0u;  // what is held now is not a press
    g_repeat = 0;
    g_capture = false;
    g_confirm = {};
    g_keys_up = true;
    g_open = true;
    LOGR("quick menu: open");
}

void Close() {
    g_open = false;
    g_capture = false;
    g_confirm = {};
    g_last_held = g_release_start = NowMs();
    g_release = true;
    LOGR("quick menu: closed");
}

// Left, Right or A on a setting
void Step(const Item& it, int dir) {
    if (g_page == kControlsPage) {
        if (it.index == 0) g_keyboard_mouse = !g_keyboard_mouse;
        else g_mouse_sensitivity = uint32_t(std::clamp(int(g_mouse_sensitivity) + dir, 1, 10));
    } else {
        const QuickRow& r = QuickPages()[size_t(g_page)].rows[size_t(it.index)];
        if (!r.step) return;
        r.step(dir);
        LOGR("quick menu: %s: %s", r.label.c_str(), r.value ? r.value().c_str() : "");
    }
    app::Save();
}

void Accept(const Item& it) {
    switch (it.kind) {
        case Kind::Setting:
            Step(it, 1);
            break;
        case Kind::Bind:
            g_capture = true;
            BeginCapture();
            break;
        case Kind::Action:
            if (g_page == kControlsPage) {
                g_confirm = {true, "Put every key back as it first was?", [] {
                                 ResetBindings();
                                 app::Save();
                             }};
            } else {
                const QuickRow& r = QuickPages()[size_t(g_page)].rows[size_t(it.index)];
                g_confirm = {true, r.confirm, r.run};
            }
            break;
        default:
            break;
    }
}

void MovePage(int by) {
    g_page = (g_page + kPages + by) % kPages;
    g_slot = 0;
}

void MoveRow(const std::vector<Item>& items, int by) {
    int n = int(items.size()), r = g_row[g_page];
    for (int i = 0; i < n; i++) {
        r = (r + n + by) % n;
        if (items[size_t(r)].kind != Kind::Header) break;
    }
    g_row[g_page] = r;
}

// what the player did this frame, whichever way
struct Acts {
    int row = 0, side = 0, page = 0;
    bool accept = false, back = false, clear = false;
    bool pad_back = false;  // B on a controller, not a key
};

Acts ReadInput() {
    Acts a;
    ImGuiIO& io = ImGui::GetIO();
    // the controllers, a held direction repeating as a menu's does
    uint32_t now = PadsHeld(), pressed = now & ~g_prev;
    g_prev = now;
    Clock::time_point t = Clock::now();
    uint32_t dirs = now & (kUp | kDown | kLeft | kRight);
    if (pressed & dirs) {
        g_repeat = pressed & dirs;
        g_repeat_at = t + std::chrono::milliseconds(380);
    } else if (dirs & g_repeat) {
        if (t >= g_repeat_at) {
            pressed |= dirs & g_repeat;
            g_repeat_at = t + std::chrono::milliseconds(85);
        }
    } else {
        g_repeat = 0;
    }
    if (pressed) g_controller_last = true;
    a.row += int((pressed & kDown) != 0) - int((pressed & kUp) != 0);
    a.side += int((pressed & kRight) != 0) - int((pressed & kLeft) != 0);
    a.page += int((pressed & kNext) != 0) - int((pressed & kPrev) != 0);
    a.accept |= (pressed & kA) != 0;
    a.back |= (pressed & kB) != 0;
    a.pad_back = (pressed & kB) != 0;
    a.clear |= (pressed & kX) != 0;
    // the keyboard, once every key held as the menu opened or a binding was made has been let go
    if (g_keys_up) {
        g_keys_up = AnyKeyDown();
        return a;
    }
    auto key = [](ImGuiKey k, bool repeat = false) { return ImGui::IsKeyPressed(k, repeat); };
    int row = int(key(ImGuiKey_DownArrow, true) || key(ImGuiKey_S, true)) - int(key(ImGuiKey_UpArrow, true) || key(ImGuiKey_W, true));
    int side = int(key(ImGuiKey_RightArrow, true) || key(ImGuiKey_D, true)) - int(key(ImGuiKey_LeftArrow, true) || key(ImGuiKey_A, true));
    int page = int(key(ImGuiKey_E) || key(ImGuiKey_PageDown) || key(ImGuiKey_Tab)) - int(key(ImGuiKey_Q) || key(ImGuiKey_PageUp));
    bool accept = key(ImGuiKey_Enter) || key(ImGuiKey_KeypadEnter) || key(ImGuiKey_Space);
    bool back = key(ImGuiKey_Escape) || key(ImGuiKey_Backspace);
    bool clear = key(ImGuiKey_Delete);
    if (row || side || page || accept || back || clear || io.MouseDelta.x != 0 || io.MouseDelta.y != 0) g_controller_last = false;
    a.row += row;
    a.side += side;
    a.page += page;
    a.accept |= accept;
    a.back |= back;
    a.clear |= clear;
    return a;
}

// ---- drawing ----

ImU32 Rgb(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
const ImU32 kPanelTop = Rgb(10, 30, 62, 244), kPanelBottom = Rgb(4, 12, 28, 244);
const ImU32 kHeadTop = Rgb(34, 78, 134, 250), kHeadBottom = Rgb(14, 40, 80, 250);
const ImU32 kRail = Rgb(170, 196, 230, 150);
const ImU32 kText = Rgb(236, 240, 248);
const ImU32 kDim = Rgb(128, 152, 190);
const ImU32 kYellow = Rgb(255, 224, 96);
const ImU32 kOrange = Rgb(236, 142, 28);
const ImU32 kOrangeBand = Rgb(222, 128, 20, 235), kOrangeFade = Rgb(222, 128, 20, 20);
const ImU32 kKey = Rgb(18, 44, 84), kKeyEdge = Rgb(70, 104, 150);

ImFont* Font() { return UiFont(); }
void Text(ImDrawList* dl, float size, float x, float y, ImU32 colour, const std::string& text) {
    dl->AddText(Font(), size, ImVec2(x, y), colour, text.c_str());
}
float Width(float size, const std::string& text) {
    ImFont* f = Font() ? Font() : ImGui::GetFont();
    return f->CalcTextSizeA(size, 1e9f, 0.0f, text.c_str()).x;
}
void Centred(ImDrawList* dl, float size, float x, float y, float w, ImU32 colour, const std::string& text) {
    Text(dl, size, x + (w - Width(size, text)) / 2, y, colour, text);
}
// the size at which text fits in w, at most size
float Fit(float size, float w, const std::string& text) {
    float need = Width(size, text);
    return need > w && need > 0 ? size * w / need : size;
}
// a setting's arrow: pointing left (-1) or right, its tip at x
void Arrow(ImDrawList* dl, float x, float y, float h, int dir, ImU32 colour) {
    float w = h * 0.6f * float(dir);
    dl->AddTriangleFilled(ImVec2(x, y + h / 2), ImVec2(x - w, y), ImVec2(x - w, y + h), colour);
}
bool Inside(ImVec2 p, float x0, float y0, float x1, float y1) { return p.x >= x0 && p.x < x1 && p.y >= y0 && p.y < y1; }

const char* Footer(const Item* it) {
    bool pad = g_controller_last;
    if (g_capture) return pad ? "PRESS THE KEY FOR IT          B: CANCEL" : "PRESS THE KEY FOR IT          LEFT CLICK: CANCEL";
    if (it && it->kind == Kind::Bind)
        return pad ? "A: CHANGE KEY      X: CLEAR      LEFT / RIGHT: KEY OR ALTERNATE      B: CLOSE"
                   : "ENTER: CHANGE KEY      DELETE: CLEAR      LEFT / RIGHT: KEY OR ALTERNATE      ESC: CLOSE";
    return pad ? "D-PAD: SELECT AND CHANGE      A: CHANGE      TRIGGERS: PAGE      B: CLOSE"
               : "ARROWS: SELECT AND CHANGE      ENTER: CHANGE      Q / E: PAGE      ESC: CLOSE";
}

// Draws the menu and takes the mouse's part of the input, which needs to know where things are
void Draw(std::vector<Item>& items, Acts& acts) {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), Rgb(0, 0, 0, 140));
    bool asking = g_confirm.open;  // as the frame began: a click that asks is not also the answer
    bool busy = g_capture || asking;
    bool moved = !busy && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0);
    bool clicked = !busy && !g_click_used && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    ImVec2 mouse = io.MousePos;

    float u = std::min(H, W / 1.5f) / 100.0f;  // a hundredth of the screen's height, less on narrow screens
    float pw = std::min(W * 0.94f, u * 122.0f), ph = u * 82.0f;
    float px = (W - pw) / 2, py = (H - ph) / 2;
    float line = std::max(1.0f, u * 0.22f);
    dl->AddRectFilledMultiColor(ImVec2(px, py), ImVec2(px + pw, py + ph), kPanelTop, kPanelTop, kPanelBottom, kPanelBottom);
    // the title band between its rails, then the pages
    float head = u * 9.0f, tabs = u * 5.6f;
    dl->AddRectFilledMultiColor(ImVec2(px, py), ImVec2(px + pw, py + head), kHeadTop, kHeadTop, kHeadBottom, kHeadBottom);
    dl->AddRectFilled(ImVec2(px, py), ImVec2(px + pw, py + line), kRail);
    dl->AddRectFilled(ImVec2(px, py + head), ImVec2(px + pw, py + head + line), kRail);
    Text(dl, u * 4.6f, px + u * 4, py + u * 2.1f, kText, "QUICK MENU");
    std::string hint = "LINKS 2004";
    Text(dl, u * 2.2f, px + pw - u * 4 - Width(u * 2.2f, hint), py + u * 3.6f, kDim, hint);
    float tw = pw / kPages, ty = py + head + line;
    for (int p = 0; p < kPages; p++) {
        float x0 = px + tw * p;
        bool on = p == g_page;
        if (clicked && Inside(mouse, x0, ty, x0 + tw, ty + tabs) && !on) {
            g_page = p;
            g_slot = 0;
            items = Items(g_page);
            on = true;
        }
        Centred(dl, u * 2.7f, x0, ty + (tabs - u * 2.7f) / 2, tw, on ? kYellow : kDim, kTitles[p]);
        if (on) dl->AddRectFilled(ImVec2(x0 + u * 2, ty + tabs - u * 0.55f), ImVec2(x0 + tw - u * 2, ty + tabs), kOrange);
    }
    dl->AddRectFilled(ImVec2(px, ty + tabs), ImVec2(px + pw, ty + tabs + line), kRail);
    float foot = u * 6.0f;
    dl->AddRectFilled(ImVec2(px, py + ph - foot), ImVec2(px + pw, py + ph - foot + line), kRail);
    dl->AddRectFilled(ImVec2(px, py + ph - line), ImVec2(px + pw, py + ph), kRail);

    // the items: as many as fit, scrolled to keep the selected one in view
    int n = int(items.size());
    int& sel = g_row[g_page];
    int& top = g_scroll[g_page];
    sel = std::clamp(sel, 0, std::max(0, n - 1));
    if (n && items[size_t(sel)].kind == Kind::Header) MoveRow(items, 1);
    float x0 = px + u * 3, x1 = px + pw - u * 3;
    float ly0 = ty + tabs + u * 1.6f, ly1 = py + ph - foot - u * 1.2f;
    if (g_page == kControlsPage) ly1 -= u * 3.4f;  // the mouse swing's line
    float rh = u * 4.5f, size = u * 2.9f;
    int fit = std::max(1, int((ly1 - ly0) / rh));
    // the wheel moves the selection, two items a notch
    if (!busy && io.MouseWheel != 0 && n) {
        int by = io.MouseWheel > 0 ? -1 : 1;
        for (int i = 0; i < 2; i++)
            if (sel + by >= 0 && sel + by < n) MoveRow(items, by);
    }
    top = std::clamp(top, sel - fit + 1, sel);
    top = std::clamp(top, 0, std::max(0, n - fit));
    // a binding's columns: what it does, the controller's button, its two keys
    float col_button = x0 + (x1 - x0) * 0.46f, col_key[2] = {x0 + (x1 - x0) * 0.66f, x0 + (x1 - x0) * 0.835f};
    float key_w = (x1 - x0) * 0.155f;
    for (int i = top; i < n && i < top + fit; i++) {
        Item& it = items[size_t(i)];
        float y = ly0 + rh * float(i - top), text_y = y + (rh - size) / 2;
        bool hover = Inside(mouse, x0, y, x1, y + rh) && it.kind != Kind::Header;
        if (hover && moved) sel = i;
        bool on = i == sel && it.kind != Kind::Header;
        if (on) dl->AddRectFilledMultiColor(ImVec2(x0, y), ImVec2(x1, y + rh - u * 0.3f), kOrangeBand, kOrangeFade, kOrangeFade, kOrangeBand);
        ImU32 colour = on ? kYellow : kText;
        switch (it.kind) {
            case Kind::Header: {
                float s = u * 2.1f, hy = y + rh - s - u * 0.9f;
                Text(dl, s, x0 + u * 2, hy, kDim, it.label);
                Text(dl, s, col_button, hy, kDim, it.button);
                for (int k = 0; k < 2; k++) Centred(dl, s, col_key[k], hy, key_w, kDim, it.keys[k]);
                dl->AddRectFilled(ImVec2(x0, y + rh - u * 0.5f), ImVec2(x1, y + rh - u * 0.5f + line), Rgb(170, 196, 230, 70));
                break;
            }
            case Kind::Setting: {
                Text(dl, size, x0 + u * 2, text_y, colour, it.label);
                float vw = Width(size, it.value), vx = x1 - u * 5 - vw;
                Text(dl, size, vx, text_y, colour, it.value);
                if (on) {
                    float ah = size * 0.62f, ay = y + (rh - ah) / 2;
                    Arrow(dl, vx - u * 2.6f, ay, ah, -1, kYellow);
                    Arrow(dl, x1 - u * 2.4f, ay, ah, 1, kYellow);
                }
                if (hover && clicked) {
                    sel = i;
                    Step(it, mouse.x < vx - u * 1.2f && mouse.x > vx - u * 5 ? -1 : 1);
                }
                break;
            }
            case Kind::Action:
                Text(dl, size, x0 + u * 2, text_y, colour, it.label);
                if (hover && clicked) {
                    sel = i;
                    Accept(it);
                }
                break;
            case Kind::Bind: {
                float s = u * 2.5f;
                float ls = Fit(s, col_button - x0 - u * 3.5f, it.label), bs = Fit(s, col_key[0] - col_button - u * 1.5f, it.button);
                Text(dl, ls, x0 + u * 2, y + (rh - ls) / 2, colour, it.label);
                Text(dl, bs, col_button, y + (rh - bs) / 2, on ? kYellow : kDim, it.button);
                for (int k = 0; k < 2; k++) {
                    float kx = col_key[k], ky = y + u * 0.5f, kh = rh - u * 1.3f;
                    bool key_on = on && g_slot == k;
                    if (hover && moved && Inside(mouse, kx, y, kx + key_w, y + rh)) g_slot = k;
                    dl->AddRectFilled(ImVec2(kx, ky), ImVec2(kx + key_w, ky + kh), key_on ? Rgb(70, 46, 14) : kKey, u * 0.4f);
                    dl->AddRect(ImVec2(kx, ky), ImVec2(kx + key_w, ky + kh), key_on ? kYellow : kKeyEdge, u * 0.4f, 0,
                                key_on ? std::max(2.0f, u * 0.3f) : line);
                    bool waiting = key_on && g_capture;
                    std::string label = waiting ? "PRESS A KEY" : it.keys[k].empty() ? "-" : it.keys[k];
                    for (char& c : label) c = char(std::toupper(static_cast<unsigned char>(c)));
                    float ks = std::min(s, s * (key_w - u) / std::max(1.0f, Width(s, label)));
                    Centred(dl, ks, kx, ky + (kh - ks) / 2, key_w, waiting ? kYellow : it.keys[k].empty() ? kDim : kText, label);
                    if (clicked && Inside(mouse, kx, y, kx + key_w, y + rh)) {
                        sel = i;
                        g_slot = k;
                        Accept(it);
                    }
                }
                break;
            }
        }
    }
    // more above or below
    if (top > 0) dl->AddTriangleFilled(ImVec2(px + pw / 2 - u, ly0 - u * 0.2f), ImVec2(px + pw / 2 + u, ly0 - u * 0.2f), ImVec2(px + pw / 2, ly0 - u * 1.2f), kDim);
    if (top + fit < n) dl->AddTriangleFilled(ImVec2(px + pw / 2 - u, ly1 + u * 0.1f), ImVec2(px + pw / 2 + u, ly1 + u * 0.1f), ImVec2(px + pw / 2, ly1 + u * 1.1f), kDim);
    if (g_page == kControlsPage) {
        std::string swing = "MOUSE SWING: HOLD LEFT CLICK, PULL THE MOUSE BACK, THEN PUSH IT FORWARD. DRIFT SIDEWAYS TO DRAW OR FADE.";
        float ss = Fit(u * 2.2f, pw - u * 6, swing);
        Centred(dl, ss, px, py + ph - foot - u * 3.6f, pw, kText, swing);
    }
    const Item* cur = n ? &items[size_t(sel)] : nullptr;
    std::string footer = Footer(cur);
    float fs = Fit(u * 2.15f, pw - u * 6, footer);
    Centred(dl, fs, px, py + ph - foot + (foot - fs) / 2, pw, kDim, footer);

    // the question an action asks first
    if (asking && g_confirm.open) {
        dl->AddRectFilled(ImVec2(px, py), ImVec2(px + pw, py + ph), Rgb(0, 0, 0, 150));
        float cw = std::min(pw * 0.86f, std::max(u * 70.0f, Width(u * 2.8f, g_confirm.text) + u * 10)), ch = u * 21.0f;
        float cx = (W - cw) / 2, cy = (H - ch) / 2;
        dl->AddRectFilledMultiColor(ImVec2(cx, cy), ImVec2(cx + cw, cy + ch), kHeadTop, kHeadTop, kPanelBottom, kPanelBottom);
        dl->AddRect(ImVec2(cx, cy), ImVec2(cx + cw, cy + ch), kRail, 0, 0, line);
        float ts = std::min(u * 2.8f, u * 2.8f * (cw - u * 6) / std::max(1.0f, Width(u * 2.8f, g_confirm.text)));
        Centred(dl, ts, cx, cy + u * 5.0f, cw, kText, g_confirm.text);
        float bw = u * 16.0f, bh = u * 5.0f, by = cy + ch - bh - u * 3.2f;
        float yes_x = cx + cw / 2 - bw - u * 2, no_x = cx + cw / 2 + u * 2;
        bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const char* yes = g_controller_last ? "A: YES" : "ENTER: YES";
        const char* no = g_controller_last ? "B: NO" : "ESC: NO";
        dl->AddRectFilled(ImVec2(yes_x, by), ImVec2(yes_x + bw, by + bh), kOrangeBand);
        Centred(dl, u * 2.5f, yes_x, by + (bh - u * 2.5f) / 2, bw, kYellow, yes);
        dl->AddRectFilled(ImVec2(no_x, by), ImVec2(no_x + bw, by + bh), kKey);
        dl->AddRect(ImVec2(no_x, by), ImVec2(no_x + bw, by + bh), kKeyEdge, 0, 0, line);
        Centred(dl, u * 2.5f, no_x, by + (bh - u * 2.5f) / 2, bw, kText, no);
        if (click && Inside(mouse, yes_x, by, yes_x + bw, by + bh)) acts.accept = true;
        if (click && Inside(mouse, no_x, by, no_x + bw, by + bh)) acts.back = true;
    }
}

void Frame() {
    Acts acts = ReadInput();
    std::vector<Item> items = Items(g_page);
    // a binding waiting for its key, any key (Esc too): the controller's B or a left click leaves it as it was
    if (g_capture) {
        int n = int(items.size());
        const Item* it = n ? &items[size_t(std::clamp(g_row[g_page], 0, n - 1))] : nullptr;
        g_click_used = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        if (acts.pad_back || g_click_used || !it || it->kind != Kind::Bind) {
            g_capture = false;
            g_keys_up = true;
        } else if (int key = CaptureKey(); key) {
            SetBinding(size_t(it->index), g_slot, key);
            app::Save();
            LOGR("quick menu: %s is now %s", it->button.c_str(), KeyName(key).c_str());
            g_capture = false;
            g_keys_up = true;
            items = Items(g_page);
        }
        acts = {};
    }
    Draw(items, acts);
    g_click_used = false;
    if (g_capture) return;
    if (g_confirm.open) {
        if (acts.accept) {
            void (*run)() = g_confirm.run;
            g_confirm = {};
            if (run) run();
        } else if (acts.back) {
            g_confirm = {};
        }
        return;
    }
    if (acts.back) {
        Close();
        return;
    }
    if (acts.page) MovePage(acts.page);
    if (acts.page) items = Items(g_page);
    int n = int(items.size());
    if (!n) return;
    if (acts.row) MoveRow(items, acts.row);
    const Item& it = items[size_t(std::clamp(g_row[g_page], 0, n - 1))];
    if (it.kind == Kind::Bind) {
        if (acts.side) g_slot = std::clamp(g_slot + acts.side, 0, 1);
        if (acts.clear) {
            SetBinding(size_t(it.index), g_slot, 0);
            app::Save();
        }
    } else if (it.kind == Kind::Setting && acts.side) {
        Step(it, acts.side);
    }
    if (acts.accept) Accept(it);
}

}  // namespace

bool QuickMenuOpen() { return g_open; }

bool KeyboardPromptsNow() {
    bool plugged = false;
    for (auto& p : g_plugged) plugged |= p.load();
    return g_keyboard_mouse && (g_keyboard_prompts || !plugged);
}

void DrawQuickMenu() {
    if (MouseSwinging()) ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    bool key = QuickMenuKeyPressed(), chord = g_toggle.exchange(false);
    if ((key || chord) && !g_capture && !DirectConnectOpen()) {
        if (g_open) Close();
        else Open();
    }
    if (g_open) Frame();
}

namespace {
// A controller's stick at the edge of its gate, pushed mostly straight one way, is the full way that way. Today's
// sticks have round gates: a backswing a few degrees off straight stops at 99% of the way back (-32639 with -2921 to
// the side, say), which the swing never counts as a full backswing, where the mouse's swing is exactly -32767. Near an
// axis the stick is stretched out toward the square of numbers the Xbox's stick has, the more the nearer it is, and
// along the diagonals (past 25 degrees off an axis) it is left round, so aiming at an angle keeps its speed. The last
// 2% of the way counts as all of it, for a stick that wears short of its edge.
void SquareStick(int16_t& sx, int16_t& sy) {
    float x = sx / 32767.0f, y = sy / 32767.0f;
    float ax = std::fabs(x), ay = std::fabs(y), most = std::max(ax, ay), least = std::min(ax, ay);
    if (most < 0.0001f) return;
    constexpr float kAxis = 0.4663f;   // tan 25 degrees: nearer an axis than that, the stick is stretched
    constexpr float kOuter = 0.98f;
    float axial = std::clamp(1.0f - (least / most) / kAxis, 0.0f, 1.0f);
    float stretch = 1.0f + (std::sqrt(x * x + y * y) / most - 1.0f) * axial;
    float k = stretch / kOuter;
    sx = int16_t(std::clamp(std::lround(x * k * 32767.0f), -32768L, 32767L));
    sy = int16_t(std::clamp(std::lround(y * k * 32767.0f), -32768L, 32767L));
}
}  // namespace

void PcPad(int port, input::Pad& pad) {
    if (port < 0 || port >= 4) return;
    // A peek (the ports' watcher thread, every 250 ms, and XInputGetCapabilities) only asks whether the port has a
    // controller: the keyboard and mouse make port 1 one. All the rest (the mouse in the menus, the quick menu's
    // chord, the prompts, DIRECT CONNECT's steering) runs for the game's own reads, on the game's thread, alone.
    if (input::Peeking()) {
        g_plugged[port] = pad.connected;
        if (port == 0 && g_keyboard_mouse) pad.connected = true;
        return;
    }
    if (pad.connected) {  // a controller's own sticks, before the keyboard and mouse add theirs
        SquareStick(pad.lx, pad.ly);
        SquareStick(pad.rx, pad.ry);
    }
    // the controller itself: the menu's input, and BACK + START
    uint32_t real = pad.connected ? Mask(pad) : 0;
    g_pads[port] = real;
    bool chord = (real & (kStart | kBack)) == (kStart | kBack);
    if (chord && !g_chord[port].exchange(chord)) g_toggle = true;
    else if (!chord) g_chord[port] = false;
    bool open = g_open, direct = DirectConnectOpen();
    g_plugged[port] = pad.connected;
    // the keyboard and mouse are controller 1's too, but not a window's that reads the keyboard itself; in the game's
    // menus the mouse points and clicks (menu_mouse.h) and does not swing
    if (port == 0 && !open && !direct) {
        KeyboardMousePad(pad, InRound() && !MenuMouseActive(), MenuTyping());
        uint32_t keys = pad.connected ? Mask(pad) : 0;
        if (g_keyboard_mouse) MenuMousePad(pad);
        // the prompts follow whichever was used last; with no controller plugged in, the keyboard
        bool plugged = false;
        for (auto& p : g_plugged) plugged |= p.load();
        if (real) g_keyboard_prompts = false;
        else if (keys || MouseSwinging() || !plugged) g_keyboard_prompts = true;
        RefreshPrompts(g_keyboard_mouse && g_keyboard_prompts);
    } else if (real) {
        g_keyboard_prompts = false;
    }
    g_game[port] = pad.connected ? Mask(pad) : 0;
    if (open) {
        Empty(pad);
    } else if (g_release) {
        uint32_t held = 0;
        for (auto& g : g_game) held |= g.load();
        int64_t now = NowMs();
        if (held) g_last_held = now;
        if (!held && now - g_last_held >= 100) g_release = false;
        else if (now - g_release_start > 4000) g_release = false;  // a button stuck down does not hold the game forever
        else Empty(pad);
    }
    DirectConnectPad(port, pad);
}

}  // namespace links
