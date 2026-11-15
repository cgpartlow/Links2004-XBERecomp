// Typing a name with the keyboard into the game's on-screen keyboard (the name entry). Moved out of pc_graphics.cpp as
// it was; MenuCameraFrame there runs it once a frame.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <cctype>
#include <string>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "common/menu_mouse.h"
#include "common/pc_controls.h"
#include "core/log.h"
#include "game/pc_graphics.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

// Typing a name. The name entry (XKeyboardMenu and the menus built on it, their init 0x92880) is an on-screen keyboard:
// a grid of keys (the menu's +0xA4, a KeyboardGridNode) worked with the D-pad and A. A typed on it (0x90B50) puts the
// selected key's character (the key's +0x30) into the name with 0x90560 (thiscall (character), ret 4), then lets a
// latched shift go (0x90B20), or runs the key's action (its name a CString at the key's +0x1C: "Backspace", "Done").
// With a keyboard, what is typed (common/menu_mouse.cpp keeps it) goes into the name the same way, each character by
// 0x90560; Backspace is the entry's own Backspace key (KeyboardBackspace). Enter stays A, pressing the selected key,
// and Esc is B, which leaves (common/pc_controls.cpp). It runs from the menus' camera each frame (MenuCameraFrame), on the game's thread,
// while the name entry is the menu manager's current menu (0x453014).
constexpr uint32_t kCurrentMenu = 0x00453014;
constexpr uint32_t kKeyboardInsert = 0x00090560, kKeyboardShiftDone = 0x00090B20;
uint32_t g_keyboard_menu = 0;
// the name entry's vtable, as it was the first time it was the current menu: a menu the game makes later at the same
// address (the name entry gone) is another class, and is not typed into
uint32_t g_keyboard_vtable = 0;

void KeyboardMenuInit(X86Context& ctx, uint8_t*) {
    if (g_keyboard_menu != ctx.ecx) LOGR("menu: the name entry is menu %08X, the keyboard types into it", ctx.ecx);
    g_keyboard_menu = ctx.ecx;
    g_keyboard_vtable = 0;
}

// a key's action as the entry's layout names it: a CString, of bytes or of UTF-16, without a leading '$', lower case
std::string KeyAction(uint32_t key) {
    uint32_t text = key ? ::kernel::R32(key + 0x1C) : 0;
    if (text < 0x10000) return {};
    uint32_t length = ::kernel::R32(text - 0x0C);
    if (!length || length > 32) return {};
    const uint8_t* p = static_cast<const uint8_t*>(::kernel::HostPtr(text));
    bool wide = length > 1 && p[1] == 0;
    std::string out;
    for (uint32_t i = 0; i < length; i++) {
        char c = char(p[wide ? i * 2 : i]);
        if (c == '$' && out.empty()) continue;
        out += char(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// Backspace: the entry's own Backspace key selected (the grid's +0x5C method) and A pressed, so it is the game's own.
// A layout without such a key gets the game's Backspace action itself (0x91030, thiscall (event), ret 4, which only
// reads the event's controller, 1 to 4 at +0x94, against the menu's controllers, a mask at +0x52).
constexpr uint32_t kKeyboardBackspace = 0x00091030;
void KeyboardBackspace(X86Context& ctx, uint32_t menu) {
    uint32_t grid = ::kernel::R32(menu + 0xA4), holder = grid ? ::kernel::R32(grid + 0x1C) : 0;
    if (!holder) return;
    ctx.ecx = holder;
    uint32_t count = ::kernel::CallGuest(ctx, ::kernel::R32(::kernel::R32(holder) + 0x20), {});
    std::string seen;
    for (uint32_t i = 0; i < count && i < 256; i++) {
        ctx.ecx = holder;
        uint32_t key = ::kernel::CallGuest(ctx, ::kernel::R32(::kernel::R32(holder) + 0x1C), {i});
        std::string action = KeyAction(key);
        if (!action.empty() && seen.size() < 200) seen += " " + action;
        if (action != "backspace") continue;
        ctx.ecx = grid;
        ::kernel::CallGuest(ctx, ::kernel::R32(::kernel::R32(grid) + 0x5C), {i});
        links::MenuTapA();
        return;
    }
    static bool told = false;
    if (!told) LOGR("menu: the name entry's %u keys have no Backspace (their actions:%s), the game's own is called", count, seen.c_str());
    told = true;
    uint32_t event = GameAlloc(ctx, 0x100);
    if (!event) return;
    std::memset(::kernel::HostPtr(event), 0, 0x100);
    uint8_t mask = *static_cast<const uint8_t*>(::kernel::HostPtr(menu + 0x52));
    uint32_t controller = 1;
    while (controller < 4 && !(mask & (1u << (controller - 1)))) controller++;
    ::kernel::W32(event + 0x94, controller);
    ctx.ecx = menu;
    ::kernel::CallGuest(ctx, kKeyboardBackspace, {event});
    GameFree(ctx, event);
}

void TypeIntoKeyboard(X86Context& ctx) {
    uint32_t menu = g_keyboard_menu;
    bool active = menu && links::g_keyboard_mouse && ::kernel::R32(kCurrentMenu) == menu;
    if (active) {
        uint32_t vtable = ::kernel::R32(menu);
        if (!g_keyboard_vtable) g_keyboard_vtable = vtable;
        active = vtable == g_keyboard_vtable;
    }
    links::SetMenuTyping(active);
    if (!active || links::MenuTapBusy()) return;
    uint32_t eax = ctx.eax, ecx = ctx.ecx, edx = ctx.edx;
    uint16_t c = 0;
    for (int n = 0; n < 8 && links::NextTyped(c); n++) {
        if (c == 8) {
            KeyboardBackspace(ctx, menu);
            break;  // what was typed after it waits for the press
        }
        ctx.ecx = menu;
        ::kernel::CallGuest(ctx, kKeyboardInsert, {uint32_t(c)});
        ctx.ecx = menu;
        ::kernel::CallGuest(ctx, kKeyboardShiftDone, {});
    }
    ctx.eax = eax;
    ctx.ecx = ecx;
    ctx.edx = edx;
}
