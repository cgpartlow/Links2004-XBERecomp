// the settings file beside the exe
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>
#include <sstream>

#include <toml++/toml.hpp>

#include "app/app.h"
#include "core/log.h"

namespace app {

namespace {

const char* kModes[] = {"fullscreen", "borderless", "windowed"};
const char* kAntiAliasing[] = {"off", "fxaa", "smaa"};  // Settings::anti_aliasing

std::filesystem::path File() { return ExeDir() / kGame.settings; }

// a device name, which is ASCII
std::string Narrow(const wchar_t* w) {
    std::string s;
    for (; *w; w++) s += char(*w < 128 ? *w : '?');
    return s;
}

BOOL CALLBACK AddMonitor(HMONITOR m, HDC, LPRECT, LPARAM list) {
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(m, &mi)) {
        reinterpret_cast<std::vector<std::pair<RECT, Monitor>>*>(list)->push_back(
            {mi.rcMonitor, Monitor{Narrow(mi.szDevice), uint32_t(mi.rcMonitor.right - mi.rcMonitor.left),
                                   uint32_t(mi.rcMonitor.bottom - mi.rcMonitor.top),
                                   (mi.dwFlags & MONITORINFOF_PRIMARY) != 0, m}});
    }
    return TRUE;
}

}  // namespace

std::vector<Monitor> Monitors() {
    std::vector<std::pair<RECT, Monitor>> found;
    EnumDisplayMonitors(nullptr, nullptr, AddMonitor, reinterpret_cast<LPARAM>(&found));
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        return a.first.left != b.first.left ? a.first.left < b.first.left : a.first.top < b.first.top;
    });
    std::vector<Monitor> out;
    for (auto& f : found) out.push_back(std::move(f.second));
    return out;
}

std::mutex& MonitorLock() {
    static std::mutex m;
    return m;
}

void* GameMonitor() {
    std::string want;
    {
        std::lock_guard<std::mutex> guard(MonitorLock());
        want = Get().monitor;
    }
    if (!want.empty())
        for (const Monitor& m : Monitors())
            if (m.device == want) return m.handle;
    return MonitorFromWindow(static_cast<HWND>(Window()), MONITOR_DEFAULTTOPRIMARY);
}

Settings& Get() {
    static Settings s;
    return s;
}

void Load() {
    Settings& s = Get();
    toml::table t;
    try {
        t = toml::parse_file(File().string());
    } catch (...) {
        return;
    }
    std::string mode = t["display_mode"].value_or(std::string(kModes[uint32_t(s.display_mode)]));
    for (uint32_t i = 0; i < 3; i++)
        if (mode == kModes[i]) s.display_mode = DisplayMode(i);
    s.monitor = t["monitor"].value_or(s.monitor);
    s.width = t["width"].value_or(s.width);
    s.height = t["height"].value_or(s.height);
    s.render_percent = t["render_percent"].value_or(s.render_percent);
    s.fps_cap = t["fps_cap"].value_or(s.fps_cap);
    s.overscan = std::min(t["overscan"].value_or(s.overscan), 50u);
    s.vsync = t["vsync"].value_or(s.vsync);
    s.aniso = t["anisotropic_filtering"].value_or(s.aniso);
    std::string aa = t["anti_aliasing"].value_or(std::string(kAntiAliasing[s.anti_aliasing % 3]));
    for (uint32_t i = 0; i < 3; i++)
        if (aa == kAntiAliasing[i]) s.anti_aliasing = i;
    s.custom_textures = t["custom_textures"].value_or(s.custom_textures);
    s.generated_textures = t["generated_textures"].value_or(s.generated_textures);
    s.dump_textures = t["dump_textures"].value_or(s.dump_textures);
    s.texture_dump_format = std::min(t["texture_dump_format"].value_or(s.texture_dump_format), 1u);
    s.join_ip = t["join_ip"].value_or(s.join_ip);
    for (const Option& o : kGame.options) {
        if (o.flag) *o.flag = t[o.key].value_or(*o.flag);
        else *o.value = t[o.key].value_or(*o.value);
    }
    if (kGame.load) kGame.load(&t);
}

namespace {

toml::table Table() {
    const Settings& s = Get();
    std::string monitor;
    {
        std::lock_guard<std::mutex> guard(MonitorLock());
        monitor = s.monitor;
    }
    toml::table t{{"monitor", monitor},
                  {"display_mode", kModes[uint32_t(s.display_mode)]},
                  {"width", int64_t(s.width)},
                  {"height", int64_t(s.height)},
                  {"render_percent", int64_t(s.render_percent)},
                  {"fps_cap", int64_t(s.fps_cap)},
                  {"overscan", int64_t(s.overscan)},
                  {"vsync", s.vsync},
                  {"anisotropic_filtering", int64_t(s.aniso)},
                  {"anti_aliasing", kAntiAliasing[s.anti_aliasing % 3]},
                  {"custom_textures", s.custom_textures},
                  {"generated_textures", s.generated_textures},
                  {"dump_textures", s.dump_textures},
                  {"texture_dump_format", int64_t(s.texture_dump_format)},
                  {"join_ip", s.join_ip}};
    for (const Option& o : kGame.options) {
        if (o.flag) t.insert(o.key, *o.flag);
        else t.insert(o.key, int64_t(*o.value));
    }
    if (kGame.save) kGame.save(&t);
    return t;
}

}  // namespace

void Save() {
    std::ofstream f(File());
    f << Table() << "\n";
}

void LogSettings() {
    std::ostringstream text;
    text << Table();
    std::istringstream lines(text.str());
    LOGR("Settings:");
    for (std::string line; std::getline(lines, line);)
        if (!line.empty()) LOGR("  %s", line.c_str());
}

Mode Desktop() {
    HMONITOR mon = static_cast<HMONITOR>(GameMonitor());
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (GetMonitorInfoW(mon, &mi) && EnumDisplaySettingsW(mi.szDevice, ENUM_REGISTRY_SETTINGS, &dm))
        return {dm.dmPelsWidth, dm.dmPelsHeight};
    return {uint32_t(GetSystemMetrics(SM_CXSCREEN)), uint32_t(GetSystemMetrics(SM_CYSCREEN))};
}

Mode Output() {
    const Settings& s = Get();
    if (s.width && s.height) return {s.width, s.height};
    return Desktop();
}

bool Widescreen() {
    // asked per frame and per HUD quad: the desktop mode lookup only runs when the resolution setting changes
    static std::atomic<uint64_t> key{~0ull};
    static std::atomic<bool> wide{true};
    const Settings& s = Get();
    uint64_t k = uint64_t(s.width) << 32 | s.height;
    if (key.exchange(k) != k) {
        Mode m = Output();
        wide = m.width * 2 >= m.height * 3;  // 3:2 or wider
    }
    return wide;
}

// Output for every frame: the desktop mode lookup reads the registry, so it runs again only when the resolution
// setting or the game's monitor changes
Mode CachedOutput() {
    struct Key {
        uint32_t width;
        uint32_t height;
        HMONITOR monitor;
        bool operator==(const Key&) const = default;
    };
    static std::mutex lock;
    static Key key{};
    static Mode mode{};
    const Settings& s = Get();
    Key now{s.width, s.height, static_cast<HMONITOR>(GameMonitor())};
    std::lock_guard<std::mutex> guard(lock);
    if (!(now == key) || !mode.height) {
        key = now;
        mode = Output();
    }
    return mode;
}

float RenderScale(uint32_t guest_height) {
    float h = CachedOutput().height * (Get().render_percent / 100.0f);
    return guest_height ? std::max(h / guest_height, 0.1f) : 1.0f;
}

}  // namespace app
