#pragma once
// MULTIPLAYER > DIRECT CONNECT: a window over the game for a host's address, "IPv4[:port]", picked digit by digit with
// a controller or typed on the keyboard. Confirmed, it becomes join_ip (the runtime's direct join address, xnet.cpp)
// and the game is steered into System Link, whose search then asks that host too.
#include <cstdint>

#include "core/input.h"

namespace links {

// opens the window; port is the controller the game listens to (its START was pressed on PRESS START)
void OpenDirectConnect(int port);
bool DirectConnectOpen();
// GameInfo::ui: draws the window while it is open, inside the ImGui frame
void DrawDirectConnect();
// GameInfo::pad: reads the controllers for the window and keeps them from the game while it is open, then presses
// UP and A on the game's controller to go from DIRECT CONNECT to System Link
void DirectConnectPad(int port, input::Pad& pad);
// GameInfo::window, before the renderer builds its font atlas: the window's font
void LoadDirectConnectFont();

}  // namespace links
