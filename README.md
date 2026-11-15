# Links 2004 - XBE Recomp

An unofficial native PC port of the original Xbox version of Links 2004, made with static recompilation and built as a 64-bit Windows executable. The game's x86 code is translated to C++ ahead of time. The Xbox kernel, Direct3D, DirectSound, XACT and XNet calls are implemented on Windows. Everything is drawn with Direct3D 12 and played through XAudio2.

This port was created with the [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp) tool by TGP482.

> [!NOTE]
> This project does not include any game assets or game code. You must provide the files from your own legally acquired copy of the game to install or build. The recompiled code is built on your PC from your own copy.

## Installation
The latest version can be downloaded from the [Releases](../../releases) page.

1. Extract the zip anywhere you like.
2. Put your game files in the `assets` folder. This can be a disc image (`.iso`), or the extracted data files with `default.xbe` and the game's folders beside it.
3. Run `Links2004.exe`. The first start compiles the game's code for your PC behind a progress window. This only happens once.

The executable checks that your `default.xbe` is the version it was made from, and tells you if it is not.

## Features
### Quick Menu
Press **F1**, or **Back + Start** on a controller, at any time. The quick menu has the port's settings on its DISPLAY, GRAPHICS and EXPERIMENTAL pages (the same ones as the PC GRAPHICS and EXPERIMENTAL menus below, plus ULTRAWIDE) and the keyboard's and mouse's keys on its CONTROLS page. A controller, the keyboard or the mouse works it, and every change is saved at once.

### Keyboard and Mouse
The whole game can be played without a controller. The keyboard and mouse act as controller 1, beside a real one if it is plugged in.

| Action | Xbox controller | Keyboard and mouse |
|---|---|---|
| Swing | Left stick down, then up | Hold left click, pull the mouse back, then push it forward |
| Draw and fade | Left stick left or right while swinging | Drift the mouse sideways while swinging |
| Menus, aim marker | D-pad | Arrow keys or W A S D |
| Select, aim marker view | A | Enter or Space |
| Back, shot type (hold), mulligan | B | Backspace or right click |
| Top view, replay | X | F |
| Y | Y | R |
| Change club | Triggers | Q and E |
| White, Black | White, Black | Z, C |
| Ball spin | Right stick | I J K L |
| Putting grid | Left stick click | G |
| Pause | Start | Esc or P |
| Back button | Back | Tab |
| Quick menu | Back + Start | F1 |

In the game's menus the mouse points and clicks: move over an item to select it, left click to choose it, and right click to go back. On the name entry screen you can simply type the name on your keyboard, with Backspace to delete. Enter presses the selected on-screen key, such as Done, and Esc goes back. On a row with a value, such as a setting or a volume slider, click the value's left or right side to change it, or turn the mouse wheel over the row. While the keyboard and mouse are the last thing you used, the game's button prompts show your keys instead of the controller's buttons, such as `[ENT]SELECT [BKS]BACK`. They switch back as soon as you touch a controller.

Every key can be changed on the quick menu's CONTROLS page: select a key, press Enter (or A), then press the new key. Each action has a key and an alternate. **MOUSE SWING SENSITIVITY** sets how far the mouse travels for a full swing, and **KEYBOARD AND MOUSE: OFF** leaves the game to the controllers.

### PC GRAPHICS Menu
The port's settings are in the game's own menus, under **Options > Graphics > PC GRAPHICS**, from the main menu or the pause menu during a round. Left and Right (or A) change the selected setting, and every change is saved at once. The list scrolls, with the game's own scroll arrows.

| Setting | What it does |
|---|---|
| **Monitor** | The monitor the game shows on, numbered left to right as Windows places them. |
| **Display** | Fullscreen, Borderless (default) or Windowed. |
| **Resolution** | Any of the monitor's resolutions, defaulting to the desktop's instead of the original 640x480. |
| **Frame Rate** | 30, 60 (default), the monitor's refresh rate, 120, 144, 165, 240 or Unlimited, for the menus and rounds alike. **Original** plays as the Xbox did: menus at 60 and rounds at 30. |
| **FPS Counter** | Shows the frame rate in the top left corner. |
| **V-Sync** | On by default. |
| **Overscan** | Trims a little from each edge of the picture, as a TV did. The game was drawn with that in mind, and some main menu course pictures leave a stray column at the left edge without it. |
| **Render Scale** | Renders the game at 0.5x to 2x the output resolution, for supersampling or for speed. |
| **Anti-Aliasing** | Off, FXAA or SMAA, over the finished frame. SMAA keeps text and fine detail sharper. |
| **Texture Filter** | Forces 2x to 16x anisotropic filtering on all textures. |
| **LOD Distance** | How far away objects keep their full detail and stay drawn, up to 4x the original, for less pop-in. |
| **Grass Distance** | How far ahead the 3D grass reaches, up to 3x the original. |
| **Field of View** | Widens what the camera sees on the course, up to 1.5x the original, as if it stood further back. Handy on ultrawide displays. Takes effect from the next camera change. |
| **Smooth Animation** | On by default. The game's animations are keyed 30 times a second, and some play key by key (the golfers' reactions after a shot, the crowd's clapping), so they step at high frame rates. This blends them so they move smoothly at any frame rate. |
| **Skip Intro** | Starts the game at PRESS START, leaving out the legal screens, the logo movies and the intro. |

### Experimental
Features still being tried out are under **Options > Graphics > EXPERIMENTAL**.

- **Upscaled UI**: The menus, HUD and fonts upscaled 4x with [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN), for sharp text and icons at high resolutions. **BUILD UPSCALED UI** closes the game and upscales the textures from your own copy of the game, on your PC, then starts the game again. Nothing of the game's is shared or downloaded. The first time, it asks to download Real-ESRGAN's official Windows release (45 MB) into `tools\realesrgan` beside the exe, and checks it before using it. The images go into `textures\generated\ui`, and **UPSCALED UI** turns them on and off at any time. Building needs a GPU with Vulkan support.

### Graphics
- **Widescreen**: The course is drawn in the proportions of a 16:9 screen. The Xbox only did that with its HDTV cable, and with any other cable it drew the course a little too wide.
- **Ultrawide**: On a display wider than 16:9 (21:9, 32:9), the course fills the screen and shows more at the sides, with the HUD at the screen's edges. The menus and movies stay 16:9 in the middle, with black either side, as the menus' art was not made to be seen past 16:9. Set `ultrawide = false` in the settings file for 16:9 with black bars instead.
- **Shader Cache**: Shaders are compiled once and kept in `shader_cache` beside the exe, so a scene only stutters the first time it is seen, not every time the game starts. The release also comes with `shaders.list`, a list of the shaders a full play session needs (how each is made, not the shaders themselves, so it holds nothing of the game's). On start, the ones your cache lacks are compiled in the background while the game boots, so even the first round's flyover plays smoothly. The list grows as you play and new shaders turn up.
- **No Pop-In on Distant Hills**: Fixes an original game bug where whole hillsides of trees could suddenly appear as the camera turned, most noticeably during a hole's flyover.
- **Higher Frame Rates**: Rounds run past the Xbox's 30 FPS, with aiming, the camera and D-pad aiming kept at the original speed.
- **Widescreen Menu Backgrounds**: The PC GRAPHICS and EXPERIMENTAL screens have the Graphics menu's widescreen bars during a round.
- **Texture Replacement**: Replaces the game's textures with `.dds` or `.png` images in the `textures\replacement` folder beside the exe.
- **Texture Dumping**: Saves every texture the game uses into `textures\dumps` beside the exe, as PNG or DDS.

### Audio
- **Custom Soundtracks**: The Xbox let you rip CDs to the hard drive and play them in games. Here, put folders of music in the `soundtracks` folder beside the exe. Each folder becomes a soundtrack, and loose files become one called "Music". MP3, WMA, M4A, AAC, FLAC and WAV are supported. New songs are converted when the game starts, with a progress window. They then show up in the game's Sound options under Soundtracks.

### Multiplayer
- **System Link**: LAN play between PCs, as with linked consoles.
- **Direct Connect**: System Link over the internet. **Multiplayer > DIRECT CONNECT** takes the host's public IP address, and a port if they chose one, picked digit by digit with the D-pad or typed on the keyboard (Ctrl+V pastes one), then goes into System Link, whose search finds the host's game there. The host forwards UDP 1000-1001 and TCP 1002 on their router to their PC, or a port of their choice and the two after it to 1000-1002, then creates a System Link game.
- **Xbox Live**: The original service is gone. The sign-in menu stays where it is, and B goes back.

### Input
- **Controller**: Xbox, PlayStation and Switch controllers, with hot plugging and rumble. Pushed mostly straight in one direction, a stick at the edge of its round gate counts as fully pushed, so a full backswing is reached as it was on the Xbox's controller.
- **Steam**: Steam Input can interfere with the controls if Steam is open in desktop mode and the game hasn't been added to Steam.
- **Quit from the Main Menu**: B on the main menu asks "Are you sure you want to quit?" and closes the game on YES.

### Miscellaneous
- **Boot Splash**: Place a `splash.png` (or `.bmp`) beside the exe to display a splash screen at launch.

## Settings
Settings are kept in `settings.toml` beside the exe. It is written on the first start, and you can edit it while the game is closed.

| Setting | Values |
|---|---|
| `monitor` | the monitor's device name (`\\.\DISPLAY2`), `""` for the primary |
| `display_mode` | `"fullscreen"`, `"borderless"`, `"windowed"` |
| `width`, `height` | output resolution, `0` for your desktop's |
| `render_percent` | render resolution, percent of the output (`50` to `200`) |
| `fps_cap` | frame rate cap, `0` for none, `1` for the monitor's refresh rate (default `60`) |
| `original_frame_rate` | `true` plays as the Xbox did: menus at 60 FPS, rounds at 30 |
| `fps_counter` | `true` or `false` |
| `vsync` | `true` or `false` |
| `ultrawide` | `true` fills a display wider than 16:9 (default), `false` keeps 16:9 between black bars |
| `overscan` | tenths of a percent trimmed from each edge, `0` to `50` (default `10`) |
| `anti_aliasing` | `"off"`, `"fxaa"` or `"smaa"` |
| `anisotropic_filtering` | `0` (as the game asks), `2`, `4`, `8` or `16` |
| `lod_distance` | percent of the game's detail distances: `100`, `150`, `200`, `300` or `400` |
| `grass_distance` | percent of the game's grass distance: `100`, `150`, `200` or `300` |
| `skip_intro` | `true` starts at PRESS START |
| `keyboard_mouse` | `true` or `false`: the keyboard and mouse play as controller 1 |
| `mouse_swing_sensitivity` | `1` to `10` (default `5`): higher needs less mouse travel for a full swing |
| `[keys]` | each action's keys, such as `a = ["Enter", "Space"]`; easiest to change on the quick menu's CONTROLS page |
| `smooth_animation` | `true` (default) or `false` |
| `field_of_view` | percent of the game's field of view: `100`, `110`, `120`, `130`, `140` or `150` |
| `custom_textures` | `true` or `false`: images in `textures\replacement` |
| `generated_textures` | `true` or `false`: images in `textures\generated`, such as the upscaled UI |
| `dump_textures` | `true` or `false` |
| `texture_dump_format` | `0` PNG, `1` DDS |
| `join_ip` | the Direct Connect host, `"ip"` or `"ip:port"` (a host name also works here), that the System Link search also asks |

## System Requirements
### Minimum
- **OS**: Windows 10 64-bit
- **CPU** with support for SSE4.2 and POPCNT (x86-64-v2)
- **GPU** with support for Direct3D 12

## Building
### 1. Clone the Repository
Clone this repository using [Git](https://git-scm.com/).
```
git clone https://github.com/<your-account>/Links2004-XBERecomp.git
```

### 2. Add the Required Game Files
Place your extracted game data files in the `assets` folder. `default.xbe` must be there, with the game's folders beside it.

> [!NOTE]
> Building does not support the ISO format, so the files must be extracted. ISOs only work for playing.

### 3. Install Dependencies
Install [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/) and select the following:
- Desktop development with C++
- C++ Clang tools for Windows
- C++ CMake tools for Windows

> [!TIP]
> `build.bat` offers to install the Clang and CMake components for you if they are missing.

### 4. Build the Project
- Run `build.bat`. The first build recompiles the entire game and takes a while.
- When it finishes, run `out\build\release\Links2004.exe`.

### Making a Release
Run `release.bat`, which needs [7-Zip](https://www.7-zip.org). It writes `Links2004-XBERecomp.zip`, containing the executable, the bundled compiler, an empty `assets` folder and, if you have played with this build, the `shaders.list` from beside the exe. It contains no game files and no game code. To give a release the most complete `shaders.list`, play through the menus and a few holes on a few courses before running it.

## Repository Layout
- `src/game`: the hooks that replace or patch the game's functions: the PC GRAPHICS and EXPERIMENTAL menus, the string table and button prompts (`pc_graphics.cpp`), the round's frame rate, aiming, detail, grass, field of view and SKIP INTRO (`course.cpp`), and typing a name (`name_entry.cpp`).
- `src/common`: the game's description for the runtime, the custom soundtrack support, the UI upscaler (`ui_upscale.cpp`, with `zip_read.cpp` for the game's packs) the Direct Connect window (`direct_connect.cpp`), the keyboard and mouse controls (`pc_controls.cpp`), the mouse in the game's menus (`menu_mouse.cpp`), the quick menu (`quick_menu.cpp`) and the font they share (`ui_font.cpp`).
- `config/game.toml`: the recompiler's config, with function and data addresses in the game's executable.
- `sdk`: the Xbox XBE Recomp recompiler and runtime, with the changes this port needed, among them the shader cache and precompile list (`sdk/runtime/gpu/shader_warm.cpp`).

## Credits
- [TGP482](https://github.com/TGP482) for [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp), the recompiler and runtime this port is built on.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) for the overridable function layout and instruction hooks.
- [SMAA](https://github.com/iryoku/smaa) by Jorge Jimenez et al. for the SMAA anti-aliasing.
- [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN) by Xintao Wang et al. for the upscaled UI. It is downloaded on your PC when you build the upscaled UI, and is not part of this project.
- [Zydis](https://github.com/zyantific/zydis) for decoding x86 instructions, [toml++](https://github.com/marzer/tomlplusplus) for the configs, [Dear ImGui](https://github.com/ocornut/imgui) for the overlay and [SDL](https://github.com/libsdl-org/SDL) for the window, input and controllers.

## License
This project is under the MIT license (see `LICENSE`). The `sdk` folder is TGP482's Xbox XBE Recomp, also under the MIT license (see `sdk/LICENSE`). Links 2004 is a trademark of its respective owners. This project is not affiliated with or endorsed by them.
