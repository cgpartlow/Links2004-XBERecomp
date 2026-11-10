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
| **Overscan** | Trims a little from each edge of the picture, as a CRT TV did. The game was drawn with that in mind, and some main menu course pictures leave a stray column at the left edge without it. |
| **Render Scale** | Renders the game at 0.5x to 2x the output resolution, for supersampling or for speed. |
| **Anti-Aliasing** | Off, FXAA or SMAA, over the finished frame. SMAA keeps text and fine detail sharper. |
| **Texture Filter** | Forces 2x to 16x anisotropic filtering on all textures. |
| **LOD Distance** | How far away objects keep their full detail and stay drawn, up to 4x the original, for less pop-in. |
| **Grass Distance** | How far ahead the 3D grass reaches, up to 3x the original. |
| **Skip Intro** | Starts the game at PRESS START, leaving out the legal screens, the logo movies and the intro. |

### Experimental
Features still being tried out are under **Options > Graphics > EXPERIMENTAL**.

- **Upscaled UI**: The menus, HUD and fonts upscaled 4x with [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN), for sharp text and icons at high resolutions. **BUILD UPSCALED UI** closes the game and upscales the textures from your own copy of the game, on your PC, then starts the game again. Nothing of the game's is shared or downloaded. The first time, it asks to download Real-ESRGAN's official Windows release (45 MB) into `tools\realesrgan` beside the exe, and checks it before using it. The images go into `textures\generated\ui`, and **UPSCALED UI** turns them on and off at any time. Building needs a GPU with Vulkan support.

### Graphics
- **Higher Frame Rates**: Rounds run past the Xbox's 30 FPS, with aiming, the camera and D-pad aiming kept at the original speed.
  
### Audio
- **Custom Soundtracks**: The Xbox let you rip CDs to the hard drive and play them in games. Here, put folders of music in the `soundtracks` folder beside the exe. Each folder becomes a soundtrack, and loose files become one called "Music". MP3, WMA, M4A, AAC, FLAC and WAV are supported. New songs are converted when the game starts, with a progress window. They then show up in the game's Sound options under Soundtracks.

### Multiplayer
- **System Link**: LAN play between PCs, as with linked consoles.
- **Xbox Live**: The original service is gone. The sign-in menu stays where it is, and B goes back.

### Input
- **Controller**: Xbox, PlayStation and Switch controllers, with hot plugging and rumble.
- **Quit from the Main Menu**: B on the main menu asks "Are you sure you want to quit?" and closes the game on YES.

### Miscellaneous
- **Boot Splash**: Place a `splash.png` (or `.bmp`) beside the exe to display a splash screen at launch.

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
Run `release.bat`, which needs [7-Zip](https://www.7-zip.org). It writes `Links2004-XBERecomp.zip`, containing the executable, the bundled compiler and an empty `assets` folder. It contains no game files and no game code.

## Repository Layout
- `src/game`: the hooks that replace or patch the game's functions, among them the PC GRAPHICS and EXPERIMENTAL menus (`pc_graphics.cpp`).
- `src/common`: the game's description for the runtime, the custom soundtrack support and the UI upscaler (`ui_upscale.cpp`, with `zip_read.cpp` for the game's packs).
- `config/game.toml`: the recompiler's config, with function and data addresses in the game's executable.
- `sdk`: the Xbox XBE Recomp recompiler and runtime, with the changes this port needed.

## Credits
- [TGP482](https://github.com/TGP482) for [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp), the recompiler and runtime this port is built on.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) for the overridable function layout and instruction hooks.
- [SMAA](https://github.com/iryoku/smaa) by Jorge Jimenez et al. for the SMAA anti-aliasing.
- [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN) by Xintao Wang et al. for the upscaled UI. It is downloaded on your PC when you build the upscaled UI, and is not part of this project.
- [Zydis](https://github.com/zyantific/zydis) for decoding x86 instructions, [toml++](https://github.com/marzer/tomlplusplus) for the configs, [Dear ImGui](https://github.com/ocornut/imgui) for the overlay and [SDL](https://github.com/libsdl-org/SDL) for the window, input and controllers.

## License
This project is under the MIT license (see `LICENSE`). The `sdk` folder is TGP482's Xbox XBE Recomp, also under the MIT license (see `sdk/LICENSE`). Links 2004 is a trademark of its respective owners. This project is not affiliated with or endorsed by them.
