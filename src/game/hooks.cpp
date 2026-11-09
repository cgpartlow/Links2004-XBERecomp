// Hooks for Links 2004 (default.xbe). A strong definition replaces the recompiled function at that address, for
// example:
//
// #include "xdk/hle.h"
//
// X86_EXTERN_FUNC(__imp__sub_00012345);
// X86_FUNC(sub_00012345) {
//     __imp__sub_00012345(ctx, base);  // the original
// }
//
// Instruction hooks are listed as [[hook]] in config/game.toml and defined here as
//   bool Name(X86Context& ctx, uint8_t* base)   (with on_true / on_false)
//   void Name(X86Context& ctx, uint8_t* base)   (without)
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <algorithm>
#include <array>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {

// The MCPX audio chip's sample counter at 0xFE80200C: a free running 32 bit count of 48 kHz frames. The runtime
// replaces DirectSound, but XACT and the XPP code read this register themselves, which on a PC is unmapped memory.
// This gives them the same counter from the host clock, counted from the first read.
uint32_t ApuSampleCounter() {
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    uint64_t us = std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - start).count();
    return static_cast<uint32_t>(us * 48 / 1000);  // wraps like the hardware counter
}

// DS_OK and pop the object pointer and one argument, as the original's ret 8
inline void DsOk2(X86Context& ctx) { ::kernel::Return(ctx, 0, 2); }

}  // namespace

// ---- instruction hooks ----

// XACT 0x3B1120: mov eax, [0xFE80200C], the engine clock. on_true continues at 0x3B1125 with eax set.
bool ApuCounterEax(X86Context& ctx, uint8_t*) {
    ctx.eax = ApuSampleCounter();
    return true;
}

// XPP 0x3D70CF: mov ebx, [0xFE80200C], between cli and sti. on_true continues at 0x3D70D5 with ebx set.
bool ApuCounterEbx(X86Context& ctx, uint8_t*) {
    ctx.ebx = ApuSampleCounter();
    return true;
}

// ---- DirectSound voice settings XACT calls that the runtime does not model ----
// The runtime's buffers and streams are host objects without the console's hardware voice, so the original
// bodies of these crash (they follow the voice pointer at +0x10). Until the runtime models them they succeed
// and change nothing. Names come from the methods' order in the library (after SetVolume: SetPitch, SetLFO,
// then SetEG and SetFilter) and from what each body writes into the voice.

// IDirectSoundBuffer_SetPitch(this, LONG): stores the pitch at voice +0x18, then programs the hardware pitch.
// The music may lose XACT's pitch variation until this maps onto SetFrequency.
X86_FUNC(sub_003A0000) { DsOk2(ctx); }
// IDirectSoundBuffer_SetLFO(this, LPCDSLFODESC)
X86_FUNC(sub_003A001C) { DsOk2(ctx); }
// 0x3A01B0 IDirectSoundBuffer_GetVoiceProperties and its stream thunk 0x3A0244 are in the runtime (dsound.cpp): XACT
// reads the DSVOICEPROPS they fill when it applies a volume, and a stub that left it unwritten crashed on the music
// volume setting
// IDirectSoundBuffer_Set3DVoiceData(this, const DS3DVOICEDATA*): copies into the voice's 3D block at +0xB8
X86_FUNC(sub_003A01CC) { DsOk2(ctx); }
// IDirectSoundBuffer_Use3DVoiceData(this, BOOL): sets or clears bit 0 of voice byte +0xB. The first crash.
X86_FUNC(sub_003A01E8) { DsOk2(ctx); }

// The stream versions. XACT calls them through jmp thunks, and the recompiler folds each method body into its
// thunk, so the overrides go on the thunks (same stack: the jmp leaves it as the call made it).
X86_FUNC(sub_003A0209) { DsOk2(ctx); }  // -> 0x39FC0D CDirectSoundStream_SetPitch
X86_FUNC(sub_003A020E) { DsOk2(ctx); }  // -> 0x39FCB1 CDirectSoundStream_SetLFO
X86_FUNC(sub_003A0249) { DsOk2(ctx); }  // -> 0x39FE9D CDirectSoundStream_Set3DVoiceData
X86_FUNC(sub_003A024E) { DsOk2(ctx); }  // -> 0x39FEEF CDirectSoundStream_Use3DVoiceData
X86_FUNC(sub_003A08A3) { DsOk2(ctx); }  // -> 0x3A078B CDirectSoundStream_SetOutputBuffer (the runtime routes streams)

// ---- movies (XMV) ----
// The game's movie open (0x1CF280) does CreateFile, an overlapped 4 KB ReadFile, GetOverlappedResult and
// XMVDecoder_CreateDecoderForPackets (0x376A40). Any failure leaves the movie object's decoder (its first field)
// null, which the game does not check: its next call of XMVDecoder_GetNextFrame then crashed. These hooks log each
// step, so the log shows which one fails. LOGR: a release build keeps only errors and reports in the log.

// 0x1CF2B7, before CreateFile: eax is the path
void LogMovieOpen(X86Context& ctx, uint8_t* base) {
    const char* path = reinterpret_cast<const char*>(base + ctx.eax);
    LOGR("movie: open %.*s", int(strnlen(path, 260)), path);
}
// 0x1CF2BC, after CreateFile: eax is the handle, -1 when the file was not opened
void LogMovieCreateFile(X86Context& ctx, uint8_t*) { LOGR("movie: CreateFile returned %08X", ctx.eax); }
// 0x1CF305, after the first ReadFile: eax is its BOOL
void LogMovieReadFile(X86Context& ctx, uint8_t*) { LOGR("movie: ReadFile returned %u", ctx.eax); }
// 0x1CF30E, after GetLastError when ReadFile returned 0: 0x3E5 (pending) is fine
void LogMovieLastError(X86Context& ctx, uint8_t*) { LOGR("movie: GetLastError %u", ctx.eax); }
// 0x1CF33F, after GetOverlappedResult: eax is its BOOL, the byte count is at [esp + 0x10] and must be 4096
void LogMovieOverlapped(X86Context& ctx, uint8_t*) {
    LOGR("movie: GetOverlappedResult returned %u, %u bytes", ctx.eax, ::kernel::R32(ctx.esp + 0x10));
}
// 0x1CF377, after XMVDecoder_CreateDecoderForPackets: eax is its HRESULT
void LogMovieDecoder(X86Context& ctx, uint8_t*) { LOGR("movie: decoder create returned %08X", ctx.eax); }

// XMVDecoder_GetNextFrame(decoder, surface, &result, timestamp), ret 0x10. With no decoder it reports
// XMV_ENDOFFILE (2), which the game (0x1CF453) takes as the movie having ended, instead of crashing.
X86_EXTERN_FUNC(__imp__sub_0037706E);
X86_FUNC(sub_0037706E) {
    uint32_t decoder = ::kernel::R32(ctx.esp + 4), result = ::kernel::R32(ctx.esp + 12);
    if (decoder) {
        __imp__sub_0037706E(ctx, base);
        return;
    }
    static bool logged = false;
    if (!logged) LOGR("movie: no decoder, reporting the movie as finished");
    logged = true;
    if (result) ::kernel::W32(result, 2);
    ::kernel::Return(ctx, 0, 4);
}

// ---- Xbox Live ----
// 0x363C1: the title's dashboard launch (a slot of its platform object's vtable at 0x3D9834), which the Xbox Live
// sign-in menu calls when you choose to go to the dashboard (to make an account, set up the network). It shuts the
// sound and other systems down (0x7F3D0, 0x1CC240, 0x198F90) and then jumps to XLaunchNewImageA(path, launch
// data). Xbox Live for the original Xbox is gone and this runtime runs no dashboard, so nothing happens: the call
// fails before anything is shut down and the menu stays, and B goes back as usual.
X86_FUNC(sub_000363C1) {
    uint32_t data = ::kernel::R32(ctx.esp + 8);
    if (data)
        LOGR("title: dashboard launch ignored (reason %u, context %08X), the game stays in its menu",
             ::kernel::R32(data), ::kernel::R32(data + 4));
    else
        LOGR("title: dashboard launch ignored, the game stays in its menu");
    ::kernel::Return(ctx, 2, 2);  // as XLaunchNewImageA failing: ERROR_FILE_NOT_FOUND, stdcall with 2 arguments
}

// ---- fatal errors ----
// 0x34BB2: the title's reboot on a fatal error, which it sets as its out of memory handlers (0x34F96..0x34FB5). On the
// console it launches d:\boom.xbe, which only shows an error picture. Here the game starts again instead: the
// runtime runs no other xbe, and the title had half shut down before asking.
X86_FUNC(sub_00034BB2) {
    LOGR("title: fatal error (caller %08X), the game starts again; the console shows d:\\boom.xbe's error screen",
         ::kernel::R32(ctx.esp));
    ::kernel::Relaunch("Links2004");
}

// ---- diagnostics ----
// 0x6F3AD: the title's heap (this in ebp) could not give an allocation; next it dumps its state and reboots through
// d:\boom.xbe (0x6F3C4). The asked size is at [esp + 0x3C], the caller's return address at [esp + 0x38].
void LogHeapFull(X86Context& ctx, uint8_t* base) {
    auto r32 = [&](uint32_t va) { uint32_t v; std::memcpy(&v, base + va, 4); return v; };
    uint32_t heap = ctx.ebp;
    LOGR("heap: %08X could not give %u bytes (0x%X) to the caller returning to %08X", heap, r32(ctx.esp + 0x3C),
         r32(ctx.esp + 0x3C), r32(ctx.esp + 0x38));
    for (uint32_t o = 0; o < 0xE0; o += 0x20)
        LOGR("heap:   +%02X %08X %08X %08X %08X %08X %08X %08X %08X", o, r32(heap + o), r32(heap + o + 4),
             r32(heap + o + 8), r32(heap + o + 12), r32(heap + o + 16), r32(heap + o + 20), r32(heap + o + 24),
             r32(heap + o + 28));
    // the calls that led here: code addresses on the stack, return addresses among them
    std::string calls;
    char b[24];
    for (uint32_t k = 0x40; k < 0x800; k += 4) {
        uint32_t v = r32(ctx.esp + k);
        if (v < 0x12000 || v >= 0x3D8470) continue;
        std::snprintf(b, sizeof b, " +%X:%08X", k, v);
        calls += b;
    }
    LOGR("heap:   code on the stack:%s", calls.c_str());

    // every block still in use. A block: +0 previous block, +4 next block, +8 bytes asked for, +C flags (bit 0 in
    // use, bits 2 up the allocation tag), +D and +E more tag bytes; the data starts at +10. The heap's first block
    // is at +70 and its end at +7C. A block whose first dword is in the image is usually an object (its vtable).
    struct Group { uint32_t count = 0, bytes = 0, first = 0; };
    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, Group> groups;
    uint32_t end = r32(heap + 0x7C), used = 0, used_bytes = 0, free_bytes = 0;
    uint32_t blk = r32(heap + 0x70);
    for (int n = 0; blk && blk < end && n < 20000; n++) {
        uint32_t next = r32(blk + 4), size = r32(blk + 8);
        uint32_t flags = r32(blk + 0xC);
        if (flags & 3) {
            uint32_t d0 = r32(blk + 0x10), d1 = r32(blk + 0x14), d2 = r32(blk + 0x18);
            LOGR("heap:   used %08X %6u bytes tag %08X data %08X %08X %08X", blk + 0x10, size, flags, d0, d1, d2);
            uint32_t kind = d0 >= 0x12000 && d0 < 0x500000 ? d0 : 0;  // an object's vtable groups its kind
            auto& g = groups[{size, flags & ~3u, kind}];
            if (!g.count) g.first = blk + 0x10;
            g.count++;
            g.bytes += size;
            used++;
            used_bytes += size;
        } else if (next > blk) {
            free_bytes += next - blk;
        }
        if (next <= blk) break;
        blk = next;
    }
    LOGR("heap:   %u blocks in use, %u bytes; about %u bytes free", used, used_bytes, free_bytes);
    std::vector<std::pair<std::tuple<uint32_t, uint32_t, uint32_t>, Group>> sorted(groups.begin(), groups.end());
    std::sort(sorted.begin(), sorted.end(), [](auto& x, auto& y) { return x.second.bytes > y.second.bytes; });
    for (size_t i = 0; i < sorted.size() && i < 40; i++) {
        auto& [k, g] = sorted[i];
        LOGR("heap:   %4u x %6u bytes = %7u, tag %08X, first word %08X, first at %08X", g.count, std::get<0>(k),
             g.bytes, std::get<1>(k), std::get<2>(k), g.first);
    }
}

// ---- start-up ----
// XAPI's media check (0x3BDDB, called by its process start-up 0x3BFB5 at 0x3C00C). Links' certificate allows only
// an original Xbox DVD, so XAPI authenticates the disc with six SCSI pass-through commands (IOCTL 0x4D014). They
// fail here, and start-up then asks to return to the dashboard (0x3BCBF), which the runtime refuses, so the game
// ran on with the rest of start-up skipped: no T: (title data) or U: (saved games) drive, and every menu option
// that needs a profile or a save did nothing. The game files on this PC are the "disc", so it passes.
X86_FUNC(sub_0003BDDB) {
    static bool logged = false;
    if (!logged) LOGR("xapi: disc authentication skipped, the game files are on this PC");
    logged = true;
    ::kernel::Return(ctx, 0, 0);  // STATUS_SUCCESS, plain ret
}
