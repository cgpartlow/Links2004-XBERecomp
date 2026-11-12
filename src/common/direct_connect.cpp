// direct_connect.h: the address window and the steering into System Link.
//
// The address is seventeen digit cells: four groups of three for the IPv4 address and five for the port, each a digit
// or empty. A controller moves between the cells with Left and Right and changes one with Up and Down; the keyboard
// types digits into them ('.' and ':' jump to the next group and the port), with Backspace, Delete, arrows, Home and
// End, Ctrl+V to paste "a.b.c.d:port", Enter to connect and Esc to leave. A group's value is its digits in order, the
// empty cells skipped, so "1", "01" and "001" are all 1.
//
// While the window is open the game gets no controller input (GameInfo::pad empties every pad), so the menu behind it
// stays put. Confirmed, the address is saved as join_ip, and once every button is let go the game's controller presses
// UP (DIRECT CONNECT is the item under System Link) and then A, as a player would.
#include "common/direct_connect.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include "app/app.h"
#include "core/log.h"
#include "imgui.h"

namespace links {

namespace {

using Clock = std::chrono::steady_clock;

// ---- the address ----

constexpr int kCells = 17;      // 4 x 3 address digits, then 5 port digits
constexpr int kPortCell = 12;   // the port's first cell
constexpr int kGroups = 5;
constexpr int kGroupStart[kGroups + 1] = {0, 3, 6, 9, 12, 17};
// the game's own ports (Links 2004: VDP 1000 for play, UDP 1001 for the System Link search, TCP 1002 to join)
constexpr const char* kGamePorts = "UDP 1000-1001 and TCP 1002";

int GroupOf(int cell) {
    for (int g = 0; g < kGroups; g++)
        if (cell < kGroupStart[g + 1]) return g;
    return kGroups - 1;
}

struct Address {
    int digit[kCells];  // 0 to 9, -1 empty
    Address() { std::fill(std::begin(digit), std::end(digit), -1); }
};

// a group's value, -1 when it has no digits
long GroupValue(const Address& a, int g) {
    long v = -1;
    for (int c = kGroupStart[g]; c < kGroupStart[g + 1]; c++)
        if (a.digit[c] >= 0) v = (v < 0 ? 0 : v * 10) + a.digit[c];
    return v;
}

// "a.b.c.d" or "a.b.c.d:port" into the cells, each group's digits from its left; false when it is not one
bool Parse(const std::string& text, Address& out) {
    Address a;
    int group = 0, cell = 0;
    bool any = false;
    for (char ch : text) {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (ch >= '0' && ch <= '9') {
            if (cell >= kGroupStart[group + 1]) return false;
            a.digit[cell++] = ch - '0';
            any = true;
        } else if (ch == '.' && group < 3) {
            group++;
            cell = kGroupStart[group];
        } else if (ch == ':' && group == 3) {
            group = 4;
            cell = kPortCell;
        } else {
            return false;
        }
    }
    if (!any || group < 3) return false;
    for (int g = 0; g < 4; g++)
        if (GroupValue(a, g) < 0 || GroupValue(a, g) > 255) return false;
    out = a;
    return true;
}

// the address as join_ip; empty with why in error when it is not complete
std::string Format(const Address& a, std::string& error) {
    char text[32];
    long v[4];
    for (int g = 0; g < 4; g++) {
        v[g] = GroupValue(a, g);
        if (v[g] < 0) {
            error = "Fill in all four numbers of the address.";
            return {};
        }
        if (v[g] > 255) {
            error = "Each number of the address is 0 to 255.";
            return {};
        }
    }
    long port = GroupValue(a, 4);
    if (port == 0 || port > 65535) {
        error = "The port is 1 to 65535, or empty for the game's own.";
        return {};
    }
    if (port > 0) std::snprintf(text, sizeof text, "%ld.%ld.%ld.%ld:%ld", v[0], v[1], v[2], v[3], port);
    else std::snprintf(text, sizeof text, "%ld.%ld.%ld.%ld", v[0], v[1], v[2], v[3]);
    return text;
}

// this PC's address on its LAN, as the System Link host would tell a friend on the same network
std::string LanAddress() {
    static std::string text = [] {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        std::string out = "unknown";
        SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(53);
        to.sin_addr.s_addr = htonl(0x08080808);  // no packet is sent: connect only picks the route
        sockaddr_in me{};
        int len = sizeof(me);
        if (s != INVALID_SOCKET && ::connect(s, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == 0 &&
            ::getsockname(s, reinterpret_cast<sockaddr*>(&me), &len) == 0) {
            char b[INET_ADDRSTRLEN] = {};
            if (inet_ntop(AF_INET, &me.sin_addr, b, sizeof b)) out = b;
        }
        if (s != INVALID_SOCKET) ::closesocket(s);
        return out;
    }();
    return text;
}

// ---- the window's state, under g_state ----

std::mutex g_state;
Address g_address;
int g_cursor = 0;
std::string g_error;
std::atomic<bool> g_open{false};
std::atomic<int> g_game_port{0};
ImFont* g_font = nullptr;

// ---- the controllers ----

// a pad's buttons in one mask: D-pad (the left stick counts) up, down, left, right, START, BACK, then A B X Y
constexpr uint32_t kUp = 1, kDown = 2, kLeft = 4, kRight = 8, kStart = 0x10, kBack = 0x20;
constexpr uint32_t kA = 0x100, kB = 0x200, kX = 0x400, kY = 0x800;
constexpr int kStick = 20000;

uint32_t Mask(const input::Pad& p) {
    uint32_t m = p.buttons & 0x3F;
    for (int i = 0; i < 4; i++)
        if (p.analog[i] > 0x40) m |= kA << i;
    if (p.ly > kStick) m |= kUp;
    if (p.ly < -kStick) m |= kDown;
    if (p.lx < -kStick) m |= kLeft;
    if (p.lx > kStick) m |= kRight;
    return m;
}

void Empty(input::Pad& p) {
    p.buttons = 0;
    std::fill(std::begin(p.analog), std::end(p.analog), uint8_t(0));
    p.lx = p.ly = p.rx = p.ry = 0;
}

std::atomic<uint32_t> g_held[4];  // each port's mask as the game last polled it

uint32_t HeldAny() {
    uint32_t m = 0;
    for (auto& h : g_held) m |= h.load();
    return m;
}

// After the window closes: the game gets nothing until every button is let go, so the A or B that closed it does not
// reach the menu; then, after a connect, UP and A on the game's controller, each held for a few frames.
enum class Steer { Idle, Release, Up, Gap, Press, Settle };
std::mutex g_steer_lock;
Steer g_steer = Steer::Idle;
bool g_connect = false;  // UP and A follow the release
Clock::time_point g_since;          // when this step began, or the buttons were last seen down
Clock::time_point g_release_start;  // when the wait for the release began

constexpr auto kHold = std::chrono::milliseconds(120);
constexpr auto kGap = std::chrono::milliseconds(160);
constexpr auto kReleased = std::chrono::milliseconds(100);
constexpr auto kGiveUp = std::chrono::seconds(4);  // a button stuck down does not hold the game forever

void StartSteer(bool connect) {
    std::lock_guard<std::mutex> l(g_steer_lock);
    g_steer = Steer::Release;
    g_connect = connect;
    g_since = g_release_start = Clock::now();
}

// ---- drawing ----

ImU32 Rgb(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
const ImU32 kPanel = Rgb(14, 17, 22, 248);
const ImU32 kEdge = Rgb(58, 66, 80);
const ImU32 kText = Rgb(232, 235, 240);
const ImU32 kDim = Rgb(150, 158, 172);
const ImU32 kAccent = Rgb(240, 146, 36);  // the menus' orange
const ImU32 kCell = Rgb(30, 35, 44);
const ImU32 kWarn = Rgb(240, 110, 90);

void Text(ImDrawList* dl, float size, ImVec2 at, ImU32 colour, const std::string& text) {
    dl->AddText(g_font, size, at, colour, text.c_str());
}
float Width(float size, const std::string& text) {
    ImFont* f = g_font ? g_font : ImGui::GetFont();
    return f->CalcTextSizeA(size, 1e9f, 0.0f, text.c_str()).x;
}
void Centred(ImDrawList* dl, float size, float x, float y, float w, ImU32 colour, const std::string& text) {
    Text(dl, size, ImVec2(x + (w - Width(size, text)) / 2, y), colour, text);
}

// ---- input ----

void Move(int by) { g_cursor = std::clamp(g_cursor + by, 0, kCells - 1); }

void Change(int by) {
    int& d = g_address.digit[g_cursor];
    d = d < 0 ? (by > 0 ? 0 : 9) : d + by;
    if (d > 9 || d < 0) d = -1;
    g_error.clear();
}

void Type(int digit) {
    g_address.digit[g_cursor] = digit;
    if (g_cursor < kCells - 1) g_cursor++;
    g_error.clear();
}

// '.' or ':': to the next group's first cell (the port's for ':')
void Jump(bool port) {
    int g = GroupOf(g_cursor);
    if (port) g_cursor = kPortCell;
    else if (g < 3 && g_cursor > kGroupStart[g]) g_cursor = kGroupStart[g + 1];
}

void Close(bool connect) {
    g_open = false;
    StartSteer(connect);
}

void Connect() {
    std::string error;
    std::string text = Format(g_address, error);
    if (text.empty()) {
        g_error = error;
        return;
    }
    {
        std::lock_guard<std::recursive_mutex> ui(app::UiLock());
        app::Get().join_ip = text;
    }
    app::Save();
    LOGR("direct connect: %s, to System Link", text.c_str());
    Close(true);
}

void Paste() {
    const char* clip = ImGui::GetClipboardText();
    Address a;
    if (clip && Parse(clip, a)) {
        g_address = a;
        g_cursor = kPortCell;
        g_error.clear();
    } else {
        g_error = "The clipboard has no address like 203.0.113.7 or 203.0.113.7:3074.";
    }
}

// a direction held down repeats, as a menu's would
struct Repeat {
    Clock::time_point next;
    uint32_t held = 0;
};

uint32_t g_prev = ~0u;  // the controllers' mask last frame; all held at first, so the press that opened it is not one
Repeat g_repeat;

void Controllers() {
    uint32_t now = HeldAny();
    uint32_t pressed = now & ~g_prev;
    g_prev = now;
    Clock::time_point t = Clock::now();
    uint32_t dirs = now & (kUp | kDown | kLeft | kRight);
    if (pressed & dirs) {
        g_repeat.held = pressed & dirs;
        g_repeat.next = t + std::chrono::milliseconds(380);
    } else if (dirs & g_repeat.held) {
        if (t >= g_repeat.next) {
            pressed |= dirs & g_repeat.held;
            g_repeat.next = t + std::chrono::milliseconds(85);
        }
    } else {
        g_repeat.held = 0;
    }
    if (pressed & kLeft) Move(-1);
    if (pressed & kRight) Move(1);
    if (pressed & kUp) Change(1);
    if (pressed & kDown) Change(-1);
    if (pressed & kX) {
        g_address = Address();
        g_cursor = 0;
        g_error.clear();
    }
    if (pressed & (kA | kStart)) Connect();
    else if (pressed & (kB | kBack)) Close(false);
}

void Keyboard() {
    ImGuiIO& io = ImGui::GetIO();
    for (ImWchar ch : io.InputQueueCharacters) {
        if (ch >= '0' && ch <= '9') Type(int(ch - '0'));
        else if (ch == '.') Jump(false);
        else if (ch == ':') Jump(true);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) Move(-1);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) Move(1);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) Change(1);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) Change(-1);
    if (ImGui::IsKeyPressed(ImGuiKey_Home)) g_cursor = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_End)) g_cursor = kCells - 1;
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) g_address.digit[g_cursor] = -1;
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        if (g_address.digit[g_cursor] < 0 && g_cursor > 0) g_cursor--;
        g_address.digit[g_cursor] = -1;
        g_error.clear();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) Paste();
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) Connect();
    else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) Close(false);
}

void Draw() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), Rgb(0, 0, 0, 150));

    float u = std::min(H, W / 1.45f) / 100.0f;  // a hundredth of the screen's height, less on narrow screens
    float pw = std::min(W * 0.92f, u * 128.0f), ph = u * 62.0f;
    float px = (W - pw) / 2, py = (H - ph) / 2;
    dl->AddRectFilled(ImVec2(px, py), ImVec2(px + pw, py + ph), kPanel, u * 0.6f);
    dl->AddRect(ImVec2(px, py), ImVec2(px + pw, py + ph), kEdge, u * 0.6f, 0, std::max(1.0f, u * 0.15f));
    dl->AddRectFilled(ImVec2(px, py), ImVec2(px + pw, py + u * 0.5f), kAccent, u * 0.6f, ImDrawFlags_RoundCornersTop);

    float x = px + u * 4, y = py + u * 3.5f, inner = pw - u * 8;
    Text(dl, u * 5.0f, ImVec2(x, y), kText, "DIRECT CONNECT");
    y += u * 6.5f;
    Text(dl, u * 2.4f, ImVec2(x, y), kDim, "Join a System Link game over the internet with the host's public IP address.");
    y += u * 6.0f;

    // the cells, with the dots and colon between the groups
    float cell = std::min(u * 6.2f, inner / 21.0f), cw = cell * 0.78f, gap = cell * 0.12f, sep = cell * 0.55f;
    float row = 0;
    for (int c = 0; c < kCells; c++) row += cw + gap;
    row += sep * 4 - gap;
    float cx = x + (inner - row) / 2;
    float label_y = y, cy = y + u * 3.2f;
    Text(dl, u * 2.1f, ImVec2(cx, label_y), kDim, "IP ADDRESS");
    float port_x = 0;
    for (int c = 0; c < kCells; c++) {
        if (c == kPortCell) port_x = cx;
        ImVec2 a(cx, cy), b(cx + cw, cy + cell);
        bool on = c == g_cursor;
        dl->AddRectFilled(a, b, on ? Rgb(64, 44, 18) : kCell, u * 0.35f);
        dl->AddRect(a, b, on ? kAccent : kEdge, u * 0.35f, 0, on ? std::max(2.0f, u * 0.3f) : std::max(1.0f, u * 0.12f));
        if (g_address.digit[c] >= 0)
            Centred(dl, cell * 0.78f, cx, cy + cell * 0.1f, cw, kText, std::string(1, char('0' + g_address.digit[c])));
        cx += cw + gap;
        int g = GroupOf(c);
        if (c + 1 == kGroupStart[g + 1] && g < 4) {
            Centred(dl, cell * 0.78f, cx - gap, cy + cell * 0.1f, sep, kDim, g < 3 ? "." : ":");
            cx += sep;
        }
    }
    Text(dl, u * 2.1f, ImVec2(port_x, label_y), kDim, "PORT (OPTIONAL)");
    y = cy + cell + u * 2.2f;
    if (!g_error.empty()) Centred(dl, u * 2.4f, x, y, inner, kWarn, g_error);
    y += u * 4.6f;

    // how to use it, then what the host does
    Centred(dl, u * 2.3f, x, y, inner, kText,
            "D-pad: Left/Right move, Up/Down change      A: Connect      B: Back      X: Clear");
    y += u * 3.4f;
    Centred(dl, u * 2.3f, x, y, inner, kText,
            "Keyboard: type the address      Enter: Connect      Esc: Back      Ctrl+V: Paste");
    y += u * 5.0f;
    Text(dl, u * 2.1f, ImVec2(x, y), kDim, std::string("Hosting: forward ") + kGamePorts + " on your router to this PC");
    y += u * 2.8f;
    Text(dl, u * 2.1f, ImVec2(x, y), kDim,
         "(" + LanAddress() + "), or a port you choose and the two after it to 1000-1002,");
    y += u * 2.8f;
    Text(dl, u * 2.1f, ImVec2(x, y), kDim, "then create a System Link game and send your public IP (and port) to the others.");
}

}  // namespace

void OpenDirectConnect(int port) {
    std::lock_guard<std::mutex> l(g_state);
    std::string current;
    {
        std::lock_guard<std::recursive_mutex> ui(app::UiLock());
        current = app::Get().join_ip;
    }
    g_address = Address();
    Parse(current, g_address);
    g_cursor = 0;
    g_error.clear();
    g_prev = ~0u;
    g_repeat = {};
    g_game_port = std::clamp(port, 0, 3);
    {
        std::lock_guard<std::mutex> s(g_steer_lock);
        g_steer = Steer::Idle;
    }
    g_open = true;
    LOGR("direct connect: open (controller %d, last address \"%s\")", g_game_port.load() + 1, current.c_str());
}

bool DirectConnectOpen() { return g_open; }

void DrawDirectConnect() {
    if (!g_open) return;
    std::lock_guard<std::mutex> l(g_state);
    Controllers();
    if (g_open) Keyboard();
    if (g_open) Draw();
}

void DirectConnectPad(int port, input::Pad& pad) {
    if (port < 0 || port >= 4) return;
    g_held[port] = pad.connected ? Mask(pad) : 0;
    if (g_open) {
        Empty(pad);
        return;
    }
    std::lock_guard<std::mutex> l(g_steer_lock);
    if (g_steer == Steer::Idle) return;
    Clock::time_point now = Clock::now();
    auto next = [&](Steer s) {
        g_steer = s;
        g_since = now;
    };
    switch (g_steer) {
        case Steer::Release:  // nothing until every button is up, then a moment more
            if (HeldAny()) {
                if (now - g_release_start > kGiveUp) next(g_connect ? Steer::Up : Steer::Idle);
                else g_since = now;
            } else if (now - g_since >= kReleased) {
                next(g_connect ? Steer::Up : Steer::Idle);
            }
            break;
        case Steer::Up:
            if (now - g_since >= kHold) next(Steer::Gap);
            break;
        case Steer::Gap:
            if (now - g_since >= kGap) next(Steer::Press);
            break;
        case Steer::Press:
            if (now - g_since >= kHold) next(Steer::Settle);
            break;
        case Steer::Settle:
            if (now - g_since >= kGap) next(Steer::Idle);
            break;
        default:
            break;
    }
    Steer s = g_steer;
    if (s == Steer::Idle) return;
    Empty(pad);
    if (port != g_game_port) return;
    if (s == Steer::Up) pad.buttons |= 0x01;  // D-pad up
    if (s == Steer::Press) pad.analog[0] = 0xFF;  // A
}

void LoadDirectConnectFont() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();  // the first is the default, the FPS counter's
    wchar_t dir[MAX_PATH] = {};
    if (!GetWindowsDirectoryW(dir, MAX_PATH)) return;
    for (const wchar_t* name : {L"\\Fonts\\segoeuib.ttf", L"\\Fonts\\segoeui.ttf", L"\\Fonts\\arialbd.ttf"}) {
        std::wstring path = std::wstring(dir) + name;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        char utf8[MAX_PATH * 3] = {};
        WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, utf8, sizeof utf8, nullptr, nullptr);
        ImFontConfig config;
        config.OversampleH = 2;
        g_font = io.Fonts->AddFontFromFileTTF(utf8, 64.0f, &config);
        if (g_font) return;
    }
}

}  // namespace links
