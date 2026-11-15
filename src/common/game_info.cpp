#include <cstdlib>

#include "app/app.h"
#include "common/direct_connect.h"
#include "common/menu_mouse.h"
#include "common/pc_controls.h"
#include "common/quick_menu.h"
#include "common/ui_font.h"
#include "common/ui_upscale.h"

namespace links {
void StartSoundtracks(void* hwnd);  // soundtracks.cpp: music from the soundtracks folder, for the game's custom soundtracks
extern bool g_fps_counter;          // pc_graphics.cpp: PC GRAPHICS > FPS COUNTER
extern bool g_original_fps;         // pc_graphics.cpp: PC GRAPHICS > FRAME RATE: ORIGINAL (rounds at 30 FPS)
extern uint32_t g_lod_percent;      // pc_graphics.cpp: PC GRAPHICS > LOD DISTANCE, percent of the game's
extern uint32_t g_grass_percent;    // pc_graphics.cpp: PC GRAPHICS > GRASS DISTANCE, percent of the game's
extern bool g_skip_intro;           // pc_graphics.cpp: PC GRAPHICS > SKIP INTRO
extern bool g_smooth_animation;     // pc_graphics.cpp: PC GRAPHICS > SMOOTH ANIMATION
extern uint32_t g_fov_percent;      // pc_graphics.cpp: PC GRAPHICS > FIELD OF VIEW, percent of the game's
void DrawPcOverlay(double fps);     // pc_graphics.cpp: the FPS counter

// each frame inside ImGui's: the port's overlay, then the quick menu over it
void Ui(double fps) {
    DrawPcOverlay(fps);
    DrawQuickMenu();
}

// the game window is up: the port's windows' font before the renderer builds the font atlas, then the soundtracks
void Window(void* hwnd) {
    LoadUiFont();
    StartSoundtracks(hwnd);
}

// started with --upscale-ui (EXPERIMENTAL > BUILD UPSCALED UI): this run is the UI upscaler, which starts the game
// again when it is done and never returns
void Init() {
    if (UpscaleUiAsked()) RunUiUpscale();
}
}

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"Links 2004",               // window and splash
    .short_title = L"Links 2004",               // message boxes
    .name = L"Links2004",                      // window classes, crash dumps, handover event
    .log = "links2004",                        // log file prefix
    .settings = "settings.toml",          // settings file beside the exe
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .images = {{"default.xbe", "Links2004"}},  // each recompiled xbe and its exe, for title launches
    .options = {{"fps_counter", &links::g_fps_counter, nullptr},  // game settings kept in the settings file
                {"original_frame_rate", &links::g_original_fps, nullptr},
                {"lod_distance", nullptr, &links::g_lod_percent},
                {"grass_distance", nullptr, &links::g_grass_percent},
                {"skip_intro", &links::g_skip_intro, nullptr},
                {"smooth_animation", &links::g_smooth_animation, nullptr},
                {"field_of_view", nullptr, &links::g_fov_percent}},
    .load = links::LoadControls,          // reads more from the settings file's toml::table: the keys
    .save = links::SaveControls,          // writes more into it
    .init = links::Init,                  // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = links::Window,              // the game window is up
    .message = links::MenuMessage,        // window messages first; true takes one: the wheel, for the menus
    .ui = links::Ui,                      // ImGui drawing each frame
    .pad = links::PcPad,                  // changes a polled pad: keyboard and mouse, the quick menu, DIRECT CONNECT
    .rumble = nullptr,                    // changes a rumble before it is set
    .draw = links::MenuDraw,              // each draw: where the menus' text is, for the mouse
};
