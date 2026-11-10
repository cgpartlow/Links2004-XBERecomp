// The UI upscaler (ui_upscale.h): a small window over a worker thread that
// 1. makes sure Real-ESRGAN ncnn-vulkan is in tools\realesrgan\ beside the exe, downloading its official Windows
//    release from GitHub once (asked first; checked against the release's SHA-256) and keeping the exe, its OpenMP
//    runtime and the realesrgan-x4plus-anime model;
// 2. reads every texture of the game's GUI packs (GuiPackages\Base.ruirr, Setup.ruirr, OnceAtStartup.ruirr where
//    present: zips of XPR files, one texture each) from the game's folder or its mounted disc image, and names each by
//    the runtime's texture key (texturedumper::ImageKey), the name the texture pack finds it by;
// 3. writes each as two images: its colour, opaque, with the colour of fully transparent pixels filled in from their
//    neighbours (else the upscaler darkens every edge that fades out), and, when it has any transparency, its alpha
//    as a grey image. Real-ESRGAN upscales alpha with plain bicubic, which blurs the letters of the fonts, whose shapes
//    are their alpha; run through the model as a picture of its own, the alpha keeps hard edges;
// 4. upscales both sets 4x with realesrgan-x4plus-anime, a model for flat colours and clean lines, which is what
//    menus, HUD art and text are;
// 5. puts colour and alpha back together into textures\generated\ui\<w>x<h>_<key>_ui.png, turns generated textures
//    on, and starts the game again.
// The window is drawn like the ones that prepare the game code (app/game_code.cpp) and convert soundtracks
// (soundtracks.cpp): a heading, the step, a bar, the share done and the time left; its close button asks, then stops.
// The tool's own log is tools\realesrgan\upscale.log; the game's log gets its summary.
#include "common/ui_upscale.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <objbase.h>
#include <wininet.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/app.h"
#include "common/zip_read.h"
#include "core/log.h"
#include "gpu/texture.h"
#include "gpu/texturedumper.h"
#include "kernel/disc.h"

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "wininet.lib")

namespace links {

namespace {

namespace fs = std::filesystem;

// Real-ESRGAN v0.2.5.0's ncnn-vulkan build for Windows (MIT), as published on its GitHub release page
const wchar_t kReleaseUrl[] =
    L"https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-windows.zip";
const char kReleaseSha256[] = "abc02804e17982a3be33675e4d471e91ea374e65b70167abc09e31acb412802d";
constexpr uint32_t kReleaseBytes = 45474481;
const char kModel[] = "realesrgan-x4plus-anime";
constexpr uint32_t kScale = 4;
// the files kept from the release: the exe, its OpenMP runtime and the model's two files
const char* const kKeep[] = {"realesrgan-ncnn-vulkan.exe", "vcomp140.dll", "models/realesrgan-x4plus-anime.param",
                             "models/realesrgan-x4plus-anime.bin", "README_windows.md"};
const char* const kPacks[] = {"Base.ruirr", "Setup.ruirr", "OnceAtStartup.ruirr"};
constexpr int kBleedPasses = 64;  // how far the colour of an edge is carried into the transparent pixels around it

fs::path ToolDir() { return app::ExeDir() / "tools" / "realesrgan"; }
fs::path WorkDir() { return ToolDir() / "work"; }
fs::path OutDir() { return app::ExeDir() / "textures" / "generated" / "ui"; }

// ---- the window and what the worker tells it ----

constexpr UINT kFinished = WM_APP + 1;

// the window's colours, as the game code and soundtrack windows have them
constexpr COLORREF kBackground = RGB(10, 11, 10);
constexpr COLORREF kText = RGB(227, 232, 219);
constexpr COLORREF kDim = RGB(145, 153, 138);
constexpr COLORREF kBarEmpty = RGB(28, 33, 26);
constexpr COLORREF kBarFull = RGB(145, 158, 130);

int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

struct Ui {
    HWND window = nullptr;
    HFONT font = nullptr;
    std::mutex lock;
    std::wstring status;     // under lock: the step
    int permille = 0;        // under lock: the bar
    int step_from = 0;       // under lock: the bar where the step began
    int64_t step_start = 0;  // under lock: when it began
    std::atomic<bool> cancelled{false};
    std::atomic<HANDLE> child{nullptr};  // the upscaler while it runs, ended by a stop
    FILE* log = nullptr;
};
Ui g;

void Log(const char* fmt, ...) {
    char line[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof line, fmt, args);
    va_end(args);
    LOGR("ui upscale: %s", line);
    if (g.log) {
        std::fprintf(g.log, "%s\n", line);
        std::fflush(g.log);
    }
}

// a new step and where the bar is at its start; the window repaints on its timer
void Status(const std::wstring& text, int permille) {
    std::lock_guard<std::mutex> l(g.lock);
    g.status = text;
    g.permille = g.step_from = std::clamp(permille, 0, 1000);
    g.step_start = NowMs();
}

void Progress(int permille) {
    std::lock_guard<std::mutex> l(g.lock);
    g.permille = std::clamp(permille, 0, 1000);
}

// a question or a message over the window, from the worker
int Ask(const wchar_t* text, UINT flags) { return MessageBoxW(g.window, text, app::kGame.title, flags); }

std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

bool ReadHostFile(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool WriteHostFile(const fs::path& p, const std::vector<uint8_t>& bytes) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return bool(f);
}

// ---- 1. the upscaler ----

bool ToolReady() {
    std::error_code ec;
    for (const char* f : kKeep)
        if (std::strcmp(f, "README_windows.md") && !fs::exists(ToolDir() / f, ec)) return false;
    return true;
}

std::string Sha256(const std::vector<uint8_t>& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    uint8_t digest[32] = {};
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
              BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0 &&
              BCryptHashData(hash, const_cast<PUCHAR>(data.data()), ULONG(data.size()), 0) == 0 &&
              BCryptFinishHash(hash, digest, sizeof digest, 0) == 0;
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) return {};
    char hex[65];
    for (int i = 0; i < 32; i++) std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return hex;
}

// the release into memory, through the system's proxy settings; GitHub redirects to its file host, which WinINet
// follows
bool Download(std::vector<uint8_t>& out) {
    HINTERNET net = InternetOpenW(L"Links2004-XBERecomp", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!net) return false;
    HINTERNET url = InternetOpenUrlW(net, kReleaseUrl, nullptr, 0,
                                     INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE, 0);
    bool ok = url != nullptr;
    out.clear();
    out.reserve(kReleaseBytes);
    uint8_t chunk[65536];
    DWORD got = 0;
    while (ok && !g.cancelled) {
        if (!InternetReadFile(url, chunk, sizeof chunk, &got)) ok = false;
        else if (got == 0) break;
        else {
            out.insert(out.end(), chunk, chunk + got);
            Progress(int(uint64_t(out.size()) * 1000 / kReleaseBytes));
        }
    }
    if (url) InternetCloseHandle(url);
    InternetCloseHandle(net);
    return ok && !g.cancelled;
}

bool EnsureTool() {
    if (ToolReady()) return true;
    Status(L"Real-ESRGAN is not here yet", 0);
    int answer = Ask(L"Upscaling the UI uses Real-ESRGAN (ncnn-vulkan build, MIT license, by Xintao Wang), which is not "
                     L"installed yet.\n\nDownload it from its official GitHub release (45 MB) into tools\\realesrgan "
                     L"beside the game?",
                     MB_ICONQUESTION | MB_YESNO);
    if (answer != IDYES) {
        Log("the upscaler was not downloaded");
        return false;
    }
    Status(L"Downloading Real-ESRGAN (45 MB)", 0);
    std::vector<uint8_t> zip;
    if (!Download(zip)) {
        if (!g.cancelled) {
            Log("the download failed (%u bytes)", unsigned(zip.size()));
            Ask(L"Real-ESRGAN could not be downloaded. Check the internet connection and try again.", MB_ICONERROR);
        }
        return false;
    }
    std::string sha = Sha256(zip);
    if (sha != kReleaseSha256) {
        Log("the download is not the release (%u bytes, sha256 %s)", unsigned(zip.size()), sha.c_str());
        Ask(L"The downloaded file is not the Real-ESRGAN release it should be, so it was not used.", MB_ICONERROR);
        return false;
    }
    Status(L"Unpacking Real-ESRGAN", 1000);
    std::vector<zip::Entry> entries;
    if (!zip::List(zip, entries)) return false;
    int kept = 0;
    for (const zip::Entry& e : entries)
        for (const char* want : kKeep)
            if (e.name == want) {
                std::vector<uint8_t> bytes;
                if (zip::Extract(zip, e, bytes) && WriteHostFile(ToolDir() / e.name, bytes)) kept++;
            }
    Log("Real-ESRGAN downloaded, sha256 %s, %d files kept in %s", sha.c_str(), kept, ToolDir().string().c_str());
    return ToolReady();
}

// ---- 2. the game's textures ----

struct Texture {
    std::string source;  // pack and file, for the log
    uint64_t key = 0;
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> bgra;
    bool alpha = false;  // any pixel less than opaque
};

// a GUI pack from the game's folder, or from its disc image when the game runs from one
bool ReadPack(const fs::path& game_dir, const char* name, std::vector<uint8_t>& out) {
    if (const kernel::Disc* disc = kernel::MountedDisc()) return disc->Load(std::string("GuiPackages\\") + name, out);
    return ReadHostFile(game_dir / "GuiPackages" / name, out);
}

uint32_t Le32(const std::vector<uint8_t>& b, size_t at) {
    uint32_t v;
    std::memcpy(&v, &b[at], 4);
    return v;
}

// An XPR: "XPR0", its whole size, the size of its header part; then D3D resource headers (Common, Data, Lock, Format,
// Size; Data the resource's offset into the data part) up to 0xFFFFFFFF; the data part starts at the header size.
// Each texture is described as the runtime describes one in memory (gpu::Describe), from its Format and Size.
void ReadXpr(const std::string& source, const std::vector<uint8_t>& x, std::vector<Texture>& out) {
    if (x.size() < 12 || std::memcmp(x.data(), "XPR0", 4) != 0) return;
    uint32_t header = Le32(x, 8);
    if (header > x.size()) return;
    for (size_t at = 12; at + 20 <= header; at += 20) {
        uint32_t common = Le32(x, at);
        if (common == 0xFFFFFFFFu || (common & 0x70000) != 0x40000) break;  // the end, or no texture
        uint32_t data = Le32(x, at + 4), fmt = Le32(x, at + 12), size = Le32(x, at + 16);
        gpu::TextureDesc d;
        d.format = (fmt >> 8) & 0xFF;
        d.cube = fmt & 4;
        d.levels = std::max<uint32_t>((fmt >> 16) & 0xF, 1);
        if (size) continue;  // linear: movie frames and copies, which packs do not replace
        d.width = 1u << ((fmt >> 20) & 0xF);
        d.height = 1u << ((fmt >> 24) & 0xF);
        uint32_t dims = (fmt >> 4) & 0xF;
        d.depth = dims == 3 ? 1u << ((fmt >> 28) & 0xF) : 1;
        d.linear = !gpu::Format(d.format).swizzled && !gpu::Format(d.format).compressed;
        d.pitch = d.width * gpu::Format(d.format).bits / 8;
        size_t start = size_t(header) + data;
        if (start + gpu::LevelSize(d, 0) > x.size()) continue;
        const uint8_t* bytes = x.data() + start;
        Texture t;
        t.source = source;
        t.key = gpu::texturedumper::ImageKey(d, bytes, nullptr);
        if (!t.key || !gpu::texturedumper::DecodeBgra(d, bytes, nullptr, t.bgra)) {
            Log("%s: format %02X %ux%u is not one packs replace", source.c_str(), d.format, d.width, d.height);
            continue;
        }
        t.width = d.width;
        t.height = d.height;
        for (size_t i = 3; i < t.bgra.size() && !t.alpha; i += 4) t.alpha = t.bgra[i] != 0xFF;
        out.push_back(std::move(t));
    }
}

bool ReadTextures(std::vector<Texture>& out) {
    Status(L"Reading the game's menus and HUD", 0);
    app::GameFiles game;
    if (!app::FindGame(game)) return false;
    for (const char* name : kPacks) {
        std::vector<uint8_t> pack;
        if (!ReadPack(game.dir, name, pack)) continue;
        std::vector<zip::Entry> entries;
        if (!zip::List(pack, entries)) {
            Log("%s is not a pack this reads", name);
            continue;
        }
        size_t before = out.size();
        for (const zip::Entry& e : entries) {
            if (e.name.size() < 4 || _stricmp(e.name.c_str() + e.name.size() - 4, ".xpr") != 0) continue;
            std::vector<uint8_t> xpr;
            if (zip::Extract(pack, e, xpr)) ReadXpr(std::string(name) + "/" + e.name, xpr, out);
            else Log("%s/%s did not unpack", name, e.name.c_str());
        }
        Log("%s: %u textures", name, unsigned(out.size() - before));
    }
    // the same image in two packs is upscaled once
    std::sort(out.begin(), out.end(), [](const Texture& a, const Texture& b) { return a.key < b.key; });
    out.erase(std::unique(out.begin(), out.end(), [](const Texture& a, const Texture& b) { return a.key == b.key; }),
              out.end());
    return !out.empty();
}

// ---- 3. colour and alpha apart ----

// the colour of fully transparent pixels from their nearest visible ones, a ring at a time; what no ring reaches
// takes the average visible colour
void Bleed(std::vector<uint8_t>& bgra, uint32_t w, uint32_t h) {
    std::vector<uint8_t> known(size_t(w) * h);
    uint64_t sum[3] = {};
    uint64_t seen = 0;
    for (size_t i = 0; i < known.size(); i++) {
        known[i] = bgra[i * 4 + 3] != 0;
        if (!known[i]) continue;
        for (int c = 0; c < 3; c++) sum[c] += bgra[i * 4 + c];
        seen++;
    }
    for (int pass = 0; pass < kBleedPasses; pass++) {
        std::vector<uint8_t> next = known;
        bool grew = false;
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++) {
                size_t i = size_t(y) * w + x;
                if (known[i]) continue;
                uint32_t acc[3] = {}, n = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = int(x) + dx, ny = int(y) + dy;
                        if (nx < 0 || ny < 0 || nx >= int(w) || ny >= int(h)) continue;
                        size_t j = size_t(ny) * w + size_t(nx);
                        if (!known[j]) continue;
                        for (int c = 0; c < 3; c++) acc[c] += bgra[j * 4 + c];
                        n++;
                    }
                if (!n) continue;
                for (int c = 0; c < 3; c++) bgra[i * 4 + c] = uint8_t(acc[c] / n);
                next[i] = 1;
                grew = true;
            }
        known.swap(next);
        if (!grew) break;
    }
    for (size_t i = 0; i < known.size(); i++)
        if (!known[i])
            for (int c = 0; c < 3; c++) bgra[i * 4 + c] = uint8_t(seen ? sum[c] / seen : 0);
}

std::string KeyName(uint64_t key) {
    char name[24];
    std::snprintf(name, sizeof name, "%016llx", static_cast<unsigned long long>(key));
    return name;
}

bool WriteInputs(const std::vector<Texture>& textures) {
    std::error_code ec;
    fs::remove_all(WorkDir(), ec);
    fs::create_directories(WorkDir() / "colour", ec);
    fs::create_directories(WorkDir() / "alpha", ec);
    int done = 0;
    for (const Texture& t : textures) {
        if (g.cancelled) return false;
        std::vector<uint8_t> colour = t.bgra;
        if (t.alpha) Bleed(colour, t.width, t.height);
        std::vector<uint8_t> alpha(colour.size());
        for (size_t i = 0; i < colour.size(); i += 4) {
            uint8_t a = t.bgra[i + 3];
            alpha[i] = alpha[i + 1] = alpha[i + 2] = a;
            alpha[i + 3] = 0xFF;
            colour[i + 3] = 0xFF;
        }
        std::string name = KeyName(t.key) + ".png";
        if (!gpu::texturedumper::WritePngFile(WorkDir() / "colour" / name, t.width, t.height, colour)) return false;
        if (t.alpha && !gpu::texturedumper::WritePngFile(WorkDir() / "alpha" / name, t.width, t.height, alpha))
            return false;
        Progress(++done * 1000 / int(textures.size()));
    }
    return true;
}

// ---- 4. the upscaler, on a folder at a time ----

size_t CountPng(const fs::path& dir) {
    size_t n = 0;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec))
        if (e.path().extension() == ".png") n++;
    return n;
}

bool Upscale(const char* set, const std::wstring& label, size_t count, int from, int to) {
    if (!count) return true;
    fs::path in = WorkDir() / set, out = WorkDir() / (std::string(set) + "_x4");
    std::error_code ec;
    fs::create_directories(out, ec);
    Status(label, from);
    std::wstring command = L"\"" + (ToolDir() / "realesrgan-ncnn-vulkan.exe").wstring() + L"\" -i \"" + in.wstring() +
                           L"\" -o \"" + out.wstring() + L"\" -n " + Widen(kModel) + L" -s " +
                           std::to_wstring(kScale) + L" -f png -m models";
    fs::path log_path = ToolDir() / (std::string("realesrgan_") + set + ".log");
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE log = CreateFileW(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, 0, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = si.hStdError = log;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                  ToolDir().c_str(), &si, &pi);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (!started) {
        Log("realesrgan-ncnn-vulkan did not start (error %lu)", GetLastError());
        return false;
    }
    g.child = pi.hProcess;
    while (WaitForSingleObject(pi.hProcess, 250) == WAIT_TIMEOUT)
        Progress(from + int(uint64_t(to - from) * std::min(CountPng(out), count) / count));
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    g.child = nullptr;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    size_t made = CountPng(out);
    Log("%s: %u of %u images upscaled, exit code %lu (its output: %s)", set, unsigned(made), unsigned(count), code,
        log_path.string().c_str());
    return !g.cancelled && made > 0;
}

// ---- 5. back together ----

int Combine(const std::vector<Texture>& textures) {
    std::error_code ec;
    fs::create_directories(OutDir(), ec);
    for (auto& e : fs::directory_iterator(OutDir(), ec))
        if (e.path().extension() == ".png") fs::remove(e.path(), ec);
    int written = 0, done = 0;
    for (const Texture& t : textures) {
        Progress(++done * 1000 / int(textures.size()));
        std::string name = KeyName(t.key) + ".png";
        uint32_t w = 0, h = 0, aw = 0, ah = 0;
        std::vector<uint8_t> colour, alpha;
        if (!gpu::texturedumper::ReadPngFile(WorkDir() / "colour_x4" / name, w, h, colour)) {
            Log("%s (%s) was not upscaled", name.c_str(), t.source.c_str());
            continue;
        }
        if (t.alpha) {
            if (!gpu::texturedumper::ReadPngFile(WorkDir() / "alpha_x4" / name, aw, ah, alpha) || aw != w || ah != h) {
                Log("%s (%s): its alpha was not upscaled", name.c_str(), t.source.c_str());
                continue;
            }
            // the model's grey, its channels averaged
            for (size_t i = 0; i < colour.size(); i += 4)
                colour[i + 3] = uint8_t((uint32_t(alpha[i]) + alpha[i + 1] + alpha[i + 2] + 1) / 3);
        } else {
            for (size_t i = 3; i < colour.size(); i += 4) colour[i] = 0xFF;
        }
        char file[64];
        std::snprintf(file, sizeof file, "%ux%u_%s_ui.png", t.width, t.height, KeyName(t.key).c_str());
        if (gpu::texturedumper::WritePngFile(OutDir() / file, w, h, colour)) written++;
    }
    return written;
}

// ---- the run ----

enum class Outcome { Done, Cancelled, Failed };

Outcome Work() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (!EnsureTool()) return g.cancelled ? Outcome::Cancelled : Outcome::Failed;
    std::vector<Texture> textures;
    if (!ReadTextures(textures)) {
        Ask(L"The game's menu packs (GuiPackages) could not be read.", MB_ICONERROR);
        return Outcome::Failed;
    }
    size_t with_alpha = size_t(std::count_if(textures.begin(), textures.end(), [](const Texture& t) { return t.alpha; }));
    Log("%u textures, %u with transparency", unsigned(textures.size()), unsigned(with_alpha));
    Status(L"Preparing " + std::to_wstring(textures.size()) + L" textures", 0);
    if (!WriteInputs(textures)) return g.cancelled ? Outcome::Cancelled : Outcome::Failed;
    if (!Upscale("colour", L"Upscaling colour (" + std::to_wstring(textures.size()) + L" textures)", textures.size(),
                 0, with_alpha ? 600 : 1000) ||
        !Upscale("alpha", L"Upscaling transparency (" + std::to_wstring(with_alpha) + L" textures)", with_alpha, 600,
                 1000)) {
        if (g.cancelled) return Outcome::Cancelled;
        Ask(L"Real-ESRGAN did not finish. It needs a GPU with Vulkan; its output is in tools\\realesrgan.",
            MB_ICONERROR);
        return Outcome::Failed;
    }
    Status(L"Saving the upscaled textures", 0);
    int written = Combine(textures);
    std::error_code ec;
    fs::remove_all(WorkDir(), ec);
    Log("%d of %u upscaled textures saved in %s", written, unsigned(textures.size()), OutDir().string().c_str());
    if (!written) return Outcome::Failed;
    app::Get().generated_textures = true;
    app::Save();
    Status(L"Done: " + std::to_wstring(written) + L" textures. Starting the game", 1000);
    return Outcome::Done;
}

// ---- the window ----

void FillBox(HDC dc, const RECT& r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

void DrawLine(HDC dc, RECT r, const std::wstring& text, COLORREF c, UINT align) {
    SetTextColor(dc, c);
    DrawTextW(dc, text.c_str(), int(text.size()), &r, align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

// to the end of the bar at the rate the step has moved it, once it has moved enough to tell
std::wstring TimeLeft(int permille, int from, int64_t start) {
    int64_t elapsed = NowMs() - start;
    if (permille - from < 20 || permille >= 1000 || elapsed < 2000) return L"";
    int64_t left = elapsed * (1000 - permille) / (permille - from) / 1000;
    if (left < 60) return L"about " + std::to_wstring(std::max<int64_t>(left, 1)) + L" s left";
    return L"about " + std::to_wstring((left + 30) / 60) + L" min left";
}

void Paint(HDC dc, const RECT& client, int unit) {
    RECT heading{client.left + unit, client.top + unit, client.right - unit, client.top + unit * 3};
    RECT stage = heading;
    OffsetRect(&stage, 0, unit * 2);
    RECT bar{stage.left, stage.bottom + unit / 2, stage.right, stage.bottom + unit / 2 + unit};
    RECT bottom{bar.left, bar.bottom + unit / 2, bar.right, bar.bottom + unit / 2 + unit * 2};
    std::wstring status;
    int permille, from;
    int64_t start;
    {
        std::lock_guard<std::mutex> l(g.lock);
        status = g.status;
        permille = g.permille;
        from = g.step_from;
        start = g.step_start;
    }
    FillBox(dc, client, kBackground);
    SetBkMode(dc, TRANSPARENT);
    DrawLine(dc, heading, L"Upscaling the game's menus, HUD and fonts for this PC.", kText, DT_LEFT);
    DrawLine(dc, stage, g.cancelled ? std::wstring(L"Stopping; the game starts as it was.") : status, kDim, DT_LEFT);
    FillBox(dc, bar, kBarEmpty);
    RECT filled = bar;
    filled.right = bar.left + LONG(int64_t(bar.right - bar.left) * permille / 1000);
    FillBox(dc, filled, kBarFull);
    DrawLine(dc, bottom, std::to_wstring(permille / 10) + L"%", kDim, DT_LEFT);
    DrawLine(dc, bottom, TimeLeft(permille, from, start), kDim, DT_RIGHT);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {  // painted off screen, then copied, so the bar does not flicker
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client;
            GetClientRect(hwnd, &client);
            HDC memory = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
            HGDIOBJ old_bitmap = SelectObject(memory, bitmap);
            HGDIOBJ old_font = SelectObject(memory, g.font);
            Paint(memory, client, MulDiv(10, GetDpiForWindow(hwnd), 96));
            BitBlt(dc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
            SelectObject(memory, old_font);
            SelectObject(memory, old_bitmap);
            DeleteObject(bitmap);
            DeleteDC(memory);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_TIMER:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case kFinished:
            DestroyWindow(hwnd);
            return 0;
        case WM_CLOSE: {  // the worker sees it, ends the upscaler and finishes; the game starts as it was
            const wchar_t* question = L"Stop upscaling the UI? The game starts again without the new textures, and "
                                      L"you can build them later from EXPERIMENTAL.";
            if (!g.cancelled && MessageBoxW(hwnd, question, app::kGame.title, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                g.cancelled = true;
                if (HANDLE child = g.child.load()) TerminateProcess(child, 1);
            }
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// centred on the primary monitor, sized for its DPI, in the message font
void MakeWindow() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
    std::wstring class_name = std::wstring(app::kGame.name) + L"UiUpscale";
    wc.lpszClassName = class_name.c_str();
    RegisterClassExW(&wc);
    UINT dpi = GetDpiForSystem();
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r{0, 0, MulDiv(560, int(dpi), 96), MulDiv(130, int(dpi), 96)};
    AdjustWindowRectExForDpi(&r, style, FALSE, 0, dpi);
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    g.window = CreateWindowExW(0, wc.lpszClassName, app::kGame.title, style, x, y, w, h, nullptr, nullptr, inst, nullptr);
    NONCLIENTMETRICSW metrics{sizeof metrics};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof metrics, &metrics, 0, dpi);
    g.font = CreateFontIndirectW(&metrics.lfMessageFont);
    SetTimer(g.window, 1, 100, nullptr);
    ShowWindow(g.window, SW_SHOW);
    SetForegroundWindow(g.window);
}

void StartGame() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring command = L"\"" + std::wstring(exe) + L"\"";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, 0, nullptr, app::ExeDir().c_str(), &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

}  // namespace

bool UpscaleUiAsked() { return std::wcsstr(GetCommandLineW(), L"--upscale-ui") != nullptr; }

bool UpscaledUiBuilt() {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(OutDir(), ec))
        if (e.path().extension() == ".png") return true;
    return false;
}

bool StartUiUpscale() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    // --relaunched: its log carries on the game's
    std::wstring command = L"\"" + std::wstring(exe) + L"\" --upscale-ui --relaunched";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, 0, nullptr, app::ExeDir().c_str(), &si, &pi)) {
        LOGE("ui upscale: the tool did not start (error %lu)", GetLastError());
        return false;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    LOGR("ui upscale: the tool started, the game closes");
    PostMessageW(static_cast<HWND>(app::Window()), WM_CLOSE, 0, 0);
    return true;
}

void RunUiUpscale() {
    std::error_code ec;
    fs::create_directories(ToolDir(), ec);
    g.log = _wfopen((ToolDir() / "upscale.log").c_str(), L"w");
    Log("upscaling the UI with %s %ux into %s", kModel, kScale, OutDir().string().c_str());
    MakeWindow();
    Outcome outcome = Outcome::Failed;
    std::thread worker([&outcome] {
        outcome = Work();
        PostMessageW(g.window, kFinished, 0, 0);
    });
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    worker.join();
    if (g.font) DeleteObject(g.font);
    Log("%s", outcome == Outcome::Done ? "done" : outcome == Outcome::Cancelled ? "cancelled" : "failed");
    if (g.log) std::fclose(g.log);
    logx::Flush();
    StartGame();
    ExitProcess(0);
}

}  // namespace links
