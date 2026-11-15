#pragma once
#include <cstdint>

namespace input {

// the console controller's report, analog buttons are 0-255
struct Pad {
    bool connected = false;
    uint16_t buttons = 0;  // dpad up/down/left/right, start, back, left thumb, right thumb
    uint8_t analog[8] = {};  // A B X Y black white left trigger right trigger
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};

void Init();  // window thread, before boot; the game code window starts it earlier
uint16_t HeldOnAnyController();  // face buttons as XINPUT_GAMEPAD_A/B/X/Y masks, GameInfo::pad left out
// the controller on that port, then GameInfo::pad. A peek only asks whether the port has a controller (the ports'
// watcher, XInputGetCapabilities): GameInfo::pad still runs, with Peeking() true, and should only say whether it
// connects the port, leaving what it does with a pad the game reads (XInputGetState) to that read alone.
Pad Poll(int port, bool peek = false);
bool Peeking();  // in GameInfo::pad: this poll is a peek
void Rumble(int port, uint16_t left, uint16_t right);

}  // namespace input
