#include "app/app.h"
#include "common/ui_upscale.h"

namespace links {
void StartSoundtracks(void* hwnd);  // soundtracks.cpp: music from the soundtracks folder, for the game's custom soundtracks
extern bool g_fps_counter;          // pc_graphics.cpp: PC GRAPHICS > FPS COUNTER
extern bool g_original_fps;         // pc_graphics.cpp: PC GRAPHICS > FRAME RATE: ORIGINAL (rounds at 30 FPS)
extern uint32_t g_lod_percent;      // pc_graphics.cpp: PC GRAPHICS > LOD DISTANCE, percent of the game's
extern uint32_t g_grass_percent;    // pc_graphics.cpp: PC GRAPHICS > GRASS DISTANCE, percent of the game's
extern bool g_skip_intro;           // pc_graphics.cpp: PC GRAPHICS > SKIP INTRO
void DrawPcOverlay(double fps);     // pc_graphics.cpp: the FPS counter

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
                {"skip_intro", &links::g_skip_intro, nullptr}},
    .load = nullptr,                      // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
    .init = links::Init,                  // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = links::StartSoundtracks,    // the game window is up
    .message = nullptr,                   // window messages first; true takes one
    .ui = links::DrawPcOverlay,           // ImGui drawing each frame
    .pad = nullptr,                       // changes a polled pad
    .rumble = nullptr,                    // changes a rumble before it is set
};
