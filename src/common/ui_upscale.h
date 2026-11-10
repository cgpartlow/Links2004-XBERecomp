#pragma once
// EXPERIMENTAL > BUILD UPSCALED UI: the menus', HUD's and fonts' textures upscaled 4x with Real-ESRGAN on this PC, from
// the player's own copy of the game, into textures\generated\ui\ (the runtime's generated texture pack). Nothing of
// the game's is shipped: the tool reads GuiPackages\*.ruirr where the game is, and downloads the upscaler itself.
namespace links {

// --upscale-ui on the command line: this run is the tool, not the game
bool UpscaleUiAsked();
// the tool's window and work; when it is done or cancelled it starts the game again, then ends this process
[[noreturn]] void RunUiUpscale();
// from the game: starts this exe as the tool and closes the game window; false when it could not start
bool StartUiUpscale();
// textures\generated\ui\ holds the upscaled images
bool UpscaledUiBuilt();

}  // namespace links
