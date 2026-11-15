#pragma once
// Keyboard and mouse play. The keyboard and mouse are a controller to the game: each of the Xbox controller's buttons
// has up to two keys (or mouse buttons), kept in the settings file's [keys] table and changed on the quick menu's
// Controls page, and the swing is the mouse: hold the left button, pull the mouse back and push it forward, as the
// left thumbstick is pulled down and pushed up. They are read into controller 1's report, beside a real controller
// there, so either can be used at any time.
#include <cstdint>
#include <string>
#include <vector>

#include "core/input.h"

namespace links {

// one of the controller's buttons or stick directions, and the quick menu's own key
struct Binding {
    const char* id;      // its name in the settings file's [keys]
    const char* button;  // on the Xbox controller
    const char* does;    // in the game
    int key[2];          // virtual key codes, 0 none
};

extern bool g_keyboard_mouse;        // settings file keyboard_mouse: off leaves the game to the controllers
extern uint32_t g_mouse_sensitivity;  // settings file mouse_swing_sensitivity, 1 to 10: less travel for a full swing

// the bindings, a copy; SetBinding changes one (taking the key from any other that had it; 0 clears the slot)
std::vector<Binding> Bindings();
void SetBinding(size_t action, int slot, int key);
void ResetBindings();
std::string KeyName(int key);  // "Enter", "Mouse Right", "F1"; empty for 0
// an action's first key as a button prompt shows it, at most three characters ("ENT", "BKS", "RMB", "F", an arrow
// key as the menus' font's arrow, 0x8B to 0x8E); empty when it has no key. `id` is the action's name in [keys].
std::string PromptKey(const char* id);
// counts every change to the bindings, so what was made from them can tell it is out of date
uint32_t BindingsVersion();

// GameInfo::load and save: keyboard_mouse, mouse_swing_sensitivity and [keys] in the settings file's toml::table
void LoadControls(const void* table);
void SaveControls(void* table);

// the keyboard and mouse into controller 1's report, which becomes a connected one; the mouse swings only while
// swing is true (a round is on)
// typing: the keyboard is being typed on (the game's name entry), so the keys that type are not buttons
void KeyboardMousePad(input::Pad& pad, bool swing, bool typing = false);
bool MouseSwinging();  // the left button is held in a swing: the cursor stays put and hidden

// for the quick menu, each frame. While the game's window is not the foreground one, no key is down.
bool QuickMenuKeyPressed();  // the quick menu's key went down since the last call
bool AnyKeyDown();           // any key or mouse button but the left one
// rebinding: after BeginCapture, CaptureKey gives the first key to go down once every key has been let go, else 0
void BeginCapture();
int CaptureKey();

}  // namespace links
