#pragma once
// What the game's PC files share (src/game): pc_graphics.cpp (the PC GRAPHICS and EXPERIMENTAL menus, the string
// table, the button prompts, the camera's aspect, the in-round backgrounds), course.cpp (the round: frame rate, aiming,
// detail and grass distance, field of view, SKIP INTRO) and name_entry.cpp (typing a name with the keyboard).
#include <cstdint>

#include "xdk/hle.h"

namespace links {
// the settings these files read (game_info.cpp lists them, pc_graphics.cpp holds them)
extern bool g_original_fps;       // original_frame_rate: rounds at the Xbox's 30 FPS
extern uint32_t g_lod_percent;    // lod_distance
extern uint32_t g_grass_percent;  // grass_distance
extern bool g_skip_intro;         // skip_intro
extern uint32_t g_fov_percent;    // field_of_view
}  // namespace links

// the game's own heap (0x6E0C0 and 0x6DF00), for what the game frees or reads as its own
uint32_t GameAlloc(X86Context& ctx, uint32_t size);
void GameFree(X86Context& ctx, uint32_t block);

// name_entry.cpp: what was typed into the name entry, once a frame from the menus' camera (MenuCameraFrame)
void TypeIntoKeyboard(X86Context& ctx);
