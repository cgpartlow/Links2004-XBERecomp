// The round on a PC: the frame rate unlocked (and what moved by the frame because of it: the stick's and the
// D-pad's aiming, the camera's turn, the detail fades), LOD DISTANCE, GRASS DISTANCE, FIELD OF VIEW and SKIP INTRO.
// Moved out of pc_graphics.cpp as they were; the settings are PC GRAPHICS's (game/pc_graphics.h).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "core/log.h"
#include "game/pc_graphics.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

// The round's main loop (0x1F04E0) updates and draws, then waits until two vertical blanks have passed since the
// frame began (0x1F0572..0x1F05A0, BlockUntilVerticalBlank): rounds at 30 FPS on a 60 Hz console. The game moves
// everything by the time a frame took (its clock 0x42AD34 +0x14, from the timer by 0x34C37), so it does not need
// the wait. 0x1F058B, the jump past the wait: taken unless FRAME RATE is ORIGINAL; the frame rate cap paces it then.
bool UnlockGameFps(X86Context&, uint8_t*) { return !links::g_original_fps; }

// Aiming is the exception: the left stick moves the aim by its deflection each frame, so at 60 FPS it moved twice as
// fast. Every stick move ends in one of two functions: 0x125C40 (to the aiming camera's vtable +0x18) and 0x125D30
// (+0x1C), called from the aim states' command handlers (0x210050, 0x215760, 0x2157C0, 0x215E10). Just after their
// dead zone and before the call (0x125D19 and 0x125E09) the stick's x ([esp + 4]) and y ([esp]) are scaled by the
// frame's time over the Xbox's 1/30 s, from the game's own clock (0x42AD34 +0x14, at most 0.1 s). FRAME RATE:
// ORIGINAL keeps them as they are.
namespace {
// the time the frame took, by the game's own clock (0x42AD34 +0x14, at most 0.1 s), 0 before it runs
float FrameTime() {
    uint32_t clock = ::kernel::R32(0x0042AD34);
    if (!clock) return 0.0f;
    uint32_t raw = ::kernel::R32(clock + 0x14);
    float dt;
    std::memcpy(&dt, &raw, 4);
    return dt > 0.0f ? dt : 0.0f;
}

void ScaleStick(X86Context& ctx, int which) {
    if (links::g_original_fps) return;
    uint32_t clock = ::kernel::R32(0x0042AD34);
    if (!clock) return;
    float dt, x, y;
    uint32_t raw = ::kernel::R32(clock + 0x14);
    std::memcpy(&dt, &raw, 4);
    if (!(dt > 0.0f)) return;
    float k = std::min(dt * 30.0f, 3.0f);
    raw = ::kernel::R32(ctx.esp + 4);
    std::memcpy(&x, &raw, 4);
    raw = ::kernel::R32(ctx.esp);
    std::memcpy(&y, &raw, 4);
    static uint32_t logged[2];
    if ((x != 0.0f || y != 0.0f) && logged[which] < 5) {
        logged[which]++;
        LOGR("aim: stick %d move %.3f %.3f, frame %.4f s, scaled by %.2f", which, x, y, dt, k);
    }
    x *= k;
    y *= k;
    std::memcpy(&raw, &x, 4);
    ::kernel::W32(ctx.esp + 4, raw);
    std::memcpy(&raw, &y, 4);
    ::kernel::W32(ctx.esp, raw);
}
}  // namespace
void ScaleAimMove(X86Context& ctx, uint8_t*) { ScaleStick(ctx, 0); }
void ScaleAimMoveTurn(X86Context& ctx, uint8_t*) { ScaleStick(ctx, 1); }

// The cameras behind them (the aiming cameras, vtables 0x3EE5C0 and 0x3EEDF8, the only ones whose moves do anything)
// step their angles by the amount times a constant (0.06) per call, no frame time: that is why the stick needs the
// scale. Their turn (vtable +0x20, 0x136370), reached through 0x125E20 (thiscall (amount), ret 4), is the same: the
// triggers raising and lowering the camera before the shot (COMMAND_CAMHEIGHT_RAISE/LOWER, 0x215820 and 0x215850)
// and the D-pad in the aim view (0x210050, 0.5 a frame) call it every frame they are held. 0x125E20, on entry: the
// amount ([esp + 4]) scaled the same way.
void ScaleCameraTurn(X86Context& ctx, uint8_t*) {
    if (links::g_original_fps) return;
    float dt = FrameTime(), v;
    if (!(dt > 0.0f)) return;
    uint32_t raw = ::kernel::R32(ctx.esp + 4);
    std::memcpy(&v, &raw, 4);
    v *= std::min(dt * 30.0f, 3.0f);
    std::memcpy(&raw, &v, 4);
    ::kernel::W32(ctx.esp + 4, raw);
}

// The objects' level of detail (trees, buildings, the crowd: 0x14F340 and its neighbours, for each object each
// frame): an object at distance d from the camera is at full detail while d <= near x k + bias, fades to its lower
// detail (for trees, a 64 x 64 picture, the imposter) out to far x k + bias, and fades out past that (its LOD entry's
// +0x34, +0x38 and +0xA0; the fade, +0x30, goes from 1, full detail, through 0 to -1, gone, by 1/30 a frame). k is
// 0x434CA0, which the camera's update (0x1254D0, every frame) works out from the camera's aspect and the hole's
// RangeMultiplier (0x434CA4, from the course's settings, CourseSettings Hole<n>, 1 to 5, set as a hole starts by
// 0x1EEB3D). 0x125983, just after it stored k: k times LOD DISTANCE.
// The fades step 1/30 a frame times 0x434CA8 (1, read nowhere else): it becomes the frame's time in 1/30 s, so they
// take as long as on the Xbox at any frame rate.
namespace {
float ReadFloat(uint32_t va) {
    uint32_t raw = ::kernel::R32(va);
    float v;
    std::memcpy(&v, &raw, 4);
    return v;
}
void WriteFloat(uint32_t va, float v) {
    uint32_t raw;
    std::memcpy(&raw, &v, 4);
    ::kernel::W32(va, raw);
}
}  // namespace

void ScaleLodDistance(X86Context&, uint8_t*) {
    constexpr uint32_t kLodScale = 0x00434CA0, kRangeMultiplier = 0x00434CA4, kLodFadeRate = 0x00434CA8;
    float scale = float(std::clamp(links::g_lod_percent, 25u, 1000u)) / 100.0f;
    float game = ReadFloat(kLodScale), range = ReadFloat(kRangeMultiplier), k = game * scale;
    WriteFloat(kLodScale, k);
    float dt = FrameTime();
    WriteFloat(kLodFadeRate, links::g_original_fps || !(dt > 0.0f) ? 1.0f : std::min(dt * 30.0f, 3.0f));
    static float logged_range = -1.0f;
    if (range != logged_range) {
        logged_range = range;
        LOGR("lod: the hole's RangeMultiplier is %.2f, detail distance multiplier %.3f, LOD DISTANCE %u%% makes it %.3f",
             range, game, links::g_lod_percent, k);
    }
}

// The course's parts are kept in a tree of nodes, each with a box (min x y z at its +4, max at +0x10), and the
// culling visitors (0x14F1F0 and its neighbours) leave out a node, and all under it, whose box is outside the camera's
// six planes: 0x14EB00, cdecl (planes, count, box), -1 outside, 0 partly inside, 1 inside, which takes each plane's
// distance to the box's min corner and adds the box's size along the plane's normal. Some nodes that only hold other
// nodes never get a box: theirs stays empty, min FLT_MAX and max -FLT_MAX, and the sums overflow to infinities. Whether
// such a node is outside then turns on the camera's angle, not on where it is: as a hole's flyover turns, a node under
// which a whole hillside of trees lies flips from outside to inside, and the hillside appears at once. A box with min
// past max is taken as partly inside, so the nodes under it are tested on their own boxes.
X86_EXTERN_FUNC(__imp__sub_0014EB00);
X86_FUNC(sub_0014EB00) {
    uint32_t box = ::kernel::R32(ctx.esp + 0x0C);
    float lo, hi;
    bool empty = false;
    for (uint32_t i = 0; i < 3 && !empty; i++) {
        uint32_t a = ::kernel::R32(box + 4 * i), b = ::kernel::R32(box + 0x0C + 4 * i);
        std::memcpy(&lo, &a, 4);
        std::memcpy(&hi, &b, 4);
        empty = !(lo <= hi);
    }
    if (!empty) return __imp__sub_0014EB00(ctx, base);
    static bool logged = false;
    if (!logged) {
        logged = true;
        LOGR("cull: a node with an empty box is tested on what is under it, not left out by the camera's angle");
    }
    ::kernel::Return(ctx, 0, 0);
}

// The 3D grass (the "VolumeGrass" pass, 0x171730): each frame the terrain's triangles inside a sphere are gathered
// (the gatherer at 0x14BB60, its triangle test 0x14DEF0) and drawn again in the grass material's own way. The sphere
// lies ahead of the camera: its centre is the camera's position plus its direction times 27 and its radius 27 (both
// read from 0x3F1C64, at 0x17195A and 0x1719D7), so the grass ends about 54 units ahead. Just after each of the two
// reads (0x171962, 0x1719DF) ScaleGrassSphere makes xmm0 27 times GRASS DISTANCE: the sphere reaches that much further.
// The gatherer keeps its indices in one index buffer made at boot (0x14BB20: 0x1B58 bytes, 3500 indices) and stops
// at 0xDA9 of them (0x14E4A3); a bigger sphere holds up to the square of GRASS DISTANCE as many triangles. The buffer
// is made for the largest choice (GrowGrassIndices) and the stop moves with it (GrassIndexRoom). It also holds up to
// 31 meshes (0x14E462: the terrain pieces the triangles come from), which cannot grow: GrassMeshesFull logs reaching
// it, past which the grass leaves out the pieces it could not hold.
namespace {
constexpr uint32_t kGrassIndexBytes = 0x1B58;  // the game's buffer, 3500 indices
constexpr uint32_t kGrassIndexScale = 9;       // the area of the largest GRASS DISTANCE, 3X
}  // namespace
void ScaleGrassSphere(X86Context& ctx, uint8_t*) {
    if (links::g_grass_percent != 100)
        ctx.xmm[0].f[0] *= float(std::clamp(links::g_grass_percent, 50u, 300u)) / 100.0f;
}

// 0x14BB2D, the call making the gatherer's index buffer: [esp] its size in bytes
void GrowGrassIndices(X86Context& ctx, uint8_t*) {
    if (::kernel::R32(ctx.esp) != kGrassIndexBytes) return;
    ::kernel::W32(ctx.esp, kGrassIndexBytes * kGrassIndexScale);
    LOGR("grass: room for %u indices, the game had %u", kGrassIndexBytes * kGrassIndexScale / 2, kGrassIndexBytes / 2);
}

// 0x14E4A3, before a triangle's three indices go in (esi the gatherer, its index count at +0x1A0): true adds them
// (0x14E4AF) while the bigger buffer has room; false leaves the game's own stop at 0xDA9
bool GrassIndexRoom(X86Context& ctx, uint8_t*) {
    if (links::g_grass_percent == 100) return false;
    return ::kernel::R32(ctx.esi + 0x1A0) + 3 <= kGrassIndexBytes * kGrassIndexScale / 2;
}

// 0x14E462, before a new mesh's entry (esi the gatherer, its mesh count at +0x1C, at most 31)
void GrassMeshesFull(X86Context& ctx, uint8_t*) {
    static int logged = 0;
    if (::kernel::R32(ctx.esi + 0x1C) == 0x1F && logged < 5) {
        logged++;
        LOGR("grass: the gatherer is full at 31 terrain pieces (%u indices, GRASS DISTANCE %u%%)",
             ::kernel::R32(ctx.esi + 0x1A0), links::g_grass_percent);
    }
}

// SKIP INTRO. The boot shows LegalScreen1 from the game's start-up (0x37ACE), then a thread (0x36BB7) waits about five
// seconds (0x1356 ms) after each legal screen and shows the next (LegalScreen2, LegalScreen3), then ShortLoad, the
// loading screen; plays the Microsoft Game Studios and XSN Sports logo movies (MGSShortL, XSNShortL, through the movie
// player 0x455228's 0x803E0, which plays one to its end); then, the first time, the Links logo without the start
// button, the intro and the PRESS START loop (linksaniwoutstbtn, linksintro, startbtnloop), else the Links logo with
// the start button (linksaniwstbtn). The PRESS START screen then loops logoloop, and START there (0x7FC90) plays the
// intro unless the boot did (the byte 0x4552FC), then opens the main menu.
// With SKIP INTRO on, the hooks go from the start straight to ShortLoad and from there to the end of the boot's
// movies, with the intro marked as played: the boot goes from the loading screen to PRESS START, which stays (START
// there is how the game picks the player's controller), and START goes to the main menu.
namespace {
constexpr uint32_t kIntroPlayed = 0x004552FC;  // byte: the boot played the intro, so START does not
}  // namespace

// 0x37BD0, about to show LegalScreen1: on true it is left out (to 0x37BDB, where the time it was shown is noted)
bool SkipLegalScreen(X86Context&, uint8_t*) { return links::g_skip_intro; }

// FIELD OF VIEW. A 3D camera sets its projection with 0x1826F0 (thiscall (field of view up and down in radians, aspect,
// near, far, flag)): it keeps the four at +0x0C and +0x190, builds the matrix (D3DXMatrixPerspectiveFovLH) and the
// frustum its objects are culled by (0x1823C0), both from the field of view it loads at 0x182700 ([esp + 0x0C] there).
// That one is widened, by its tangent so 1.5X shows 1.5 times as much each way at the same distance, as if the camera
// stood further back; the one the camera keeps stays the game's own, so nothing that reads it back widens twice.
void CameraFieldOfView(X86Context& ctx, uint8_t*) {
    if (links::g_fov_percent <= 100) return;
    float* fov = static_cast<float*>(::kernel::HostPtr(ctx.esp + 0x0C));
    if (!(*fov > 0.01f) || !(*fov < 3.0f)) return;
    float wide = 2.0f * std::atan(std::tan(*fov * 0.5f) * float(std::min(links::g_fov_percent, 200u)) / 100.0f);
    if (wide > 0.01f && wide < 3.0f) *fov = wide;
}

// 0x36BD2, the boot thread just started: on true, past the three waits and LegalScreen2 and 3, to ShortLoad (0x36C4E)
bool SkipLegalScreens(X86Context&, uint8_t*) {
    if (links::g_skip_intro) LOGR("boot: SKIP INTRO, the legal screens, logo movies and intro are left out");
    return links::g_skip_intro;
}

// 0x36CA7, the boot's movies about to play: on true, past all of them (to 0x36DB0, which frees the movie name's
// CString, made just before here, and ends the thread), the intro marked as played. The frame's unwind state, which
// the skipped code sets to 1 as the CString is made, is set too.
bool SkipBootMovies(X86Context& ctx, uint8_t*) {
    if (!links::g_skip_intro) return false;
    *static_cast<uint8_t*>(::kernel::HostPtr(ctx.ebp - 4)) = 1;
    *static_cast<uint8_t*>(::kernel::HostPtr(kIntroPlayed)) = 1;
    return true;
}

// The D-pad, which aims before the shot (idlehuman.cm: COMMAND_AIM_TURNLEFT/RIGHT, MOVEFORWARD/BACKWARD), comes to
// the aim marker's turn (0x208B40) and move (0x208AA0) once a frame while held: thiscall (event, direction), ret 8,
// the event's state at +0x10 (1 pressed, 2 held, else released). Each call steps the marker by an amount that grows
// with how long the button has been held (by the game clock), so a frame is a step. Held calls go through at the
// Xbox's rate, one per 1/30 s of game time, and the rest return at once; presses and releases always go through.
namespace {
float g_dpad_time[2];  // game time held since the last step went through: move, turn

// true when this call should step the marker
bool DpadStep(X86Context& ctx, int which) {
    uint32_t event = ::kernel::R32(ctx.esp + 4);
    uint32_t state = event ? ::kernel::R32(event + 0x10) : 0;
    if (links::g_original_fps || state != 2) {
        g_dpad_time[which] = 0.0f;
        return true;
    }
    constexpr float kStep = 1.0f / 30.0f;
    g_dpad_time[which] += FrameTime();
    if (g_dpad_time[which] + 0.0005f < kStep) {
        static int logged = 0;
        if (logged++ < 3) LOGR("aim: D-pad %s held, a step waits (frame %.4f s)", which ? "turn" : "move", FrameTime());
        return false;
    }
    g_dpad_time[which] = std::min(g_dpad_time[which] - kStep, kStep);
    return true;
}
}  // namespace

// The aim state's thunks 0x215F10 and 0x215F20 jump to them, and the recompiler folds each body into its thunk, so the
// overrides go on the thunks (same arguments: the jump leaves the stack as the call made it).
X86_EXTERN_FUNC(__imp__sub_00215F10);
X86_FUNC(sub_00215F10) {
    if (DpadStep(ctx, 0)) return __imp__sub_00215F10(ctx, base);
    ::kernel::Return(ctx, 0, 2);
}
X86_EXTERN_FUNC(__imp__sub_00215F20);
X86_FUNC(sub_00215F20) {
    if (DpadStep(ctx, 1)) return __imp__sub_00215F20(ctx, base);
    ::kernel::Return(ctx, 0, 2);
}
