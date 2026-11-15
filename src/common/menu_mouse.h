#pragma once
// The mouse in the game's own menus: moving over an item of a list selects it, and a left click chooses it.
//
// The game has no notion of a mouse, and its menus are 3D scenes, so where an item is on screen comes from the picture
// itself: every piece of text the game draws is noted with the screen rectangle it lands in (MenuDraw, from the
// runtime's GameInfo::draw), and the lists the game has made are known (NoteMenuList, from hooks on the list's
// constructor, selection and destructor). A list whose shown items' texts are all on screen, in order, is on screen,
// and each item is where its text is. The mouse then works the menu as a player would: the D-pad is pressed until the
// selection is the item under the cursor, and A is pressed for a click, on controller 1 (the keyboard's and mouse's).
// A click on an item's value or arrows (or the wheel over the item) is Left or Right instead, and a grid's cells are
// found by the picture drawn once for each.
#include <cstdint>

#include "app/app.h"
#include "core/input.h"

namespace links {

void MenuDraw(const app::DrawProbe& draw);  // GameInfo::draw
void NoteMenuList(uint32_t list);           // a list was made or its selection set
void ForgetMenuList(uint32_t list);         // a list is being destroyed
void NoteMenuGrid(uint32_t grid);           // the same for a grid (club selection, the golfers)
void ForgetMenuGrid(uint32_t grid);
// GameInfo::message: the wheel, which changes the value of the item it is over; never takes the message
bool MenuMessage(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);

// Typing a name. While the game's name entry is up (SetMenuTyping, from the game's thread each frame) what is typed
// on the keyboard is kept, in order, for the game's side to put into the name (NextTyped: a character, or 8 for
// Backspace), and the keys that type are not the controller's buttons. MenuTapA presses A once, for
// the entry's own Backspace and Done keys; MenuTapBusy is true until that press is over.
void SetMenuTyping(bool typing);
bool MenuTyping();
bool NextTyped(uint16_t& character);
void MenuTapA();
bool MenuTapBusy();
// controller 1's poll: follows the mouse and presses the D-pad and A for it
void MenuMousePad(input::Pad& pad);
// a list was on screen at the last poll: a left click there is the menu's, not a swing
bool MenuMouseActive();

}  // namespace links
