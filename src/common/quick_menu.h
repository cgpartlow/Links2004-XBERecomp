#pragma once
// The quick menu: the port's settings and the controls over the game at any time, opened with F1 (a binding) or
// BACK + START on a controller. Its pages are DISPLAY, GRAPHICS and EXPERIMENTAL, the settings the game's own
// PC GRAPHICS and EXPERIMENTAL menus have (QuickPages, pc_graphics.cpp), and CONTROLS, the keyboard's and mouse's
// bindings (pc_controls.h). A controller, the keyboard or the mouse works it; the game gets no input while it is open.
#include <string>
#include <vector>

#include "core/input.h"

namespace links {

// a setting on a page: Left and Right step it; with `confirm`, A asks that and runs `run` on yes
struct QuickRow {
    std::string label;
    std::string (*value)();
    void (*step)(int dir);
    const char* confirm = nullptr;
    void (*run)() = nullptr;
};
struct QuickPage {
    const char* title;
    std::vector<QuickRow> rows;
};
const std::vector<QuickPage>& QuickPages();  // pc_graphics.cpp
bool InRound();                               // pc_graphics.cpp: a round is on
// pc_graphics.cpp, on the game's thread: the button prompts in the game's text are keys' names while the keyboard
// and mouse were the last used, the controller's buttons otherwise
void RefreshPrompts(bool keyboard);
// the prompts would be the keyboard's now: it was the last used, or no controller is plugged in
bool KeyboardPromptsNow();

bool QuickMenuOpen();
// GameInfo::ui: opens and closes the menu and draws it, inside the ImGui frame
void DrawQuickMenu();
// GameInfo::pad: the keyboard and mouse into controller 1, the menu's own reading of the controllers, nothing to the
// game while the menu is open or until every button is let go after it closes; then DIRECT CONNECT's
void PcPad(int port, input::Pad& pad);

}  // namespace links
