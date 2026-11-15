// ui_font.h: the port's windows' font, from the Windows fonts folder
#include "common/ui_font.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

#include "imgui.h"

namespace links {

namespace {
ImFont* g_font = nullptr;
}

ImFont* UiFont() { return g_font; }

void LoadUiFont() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();  // the first is the default, the FPS counter's
    wchar_t dir[MAX_PATH] = {};
    if (!GetWindowsDirectoryW(dir, MAX_PATH)) return;
    for (const wchar_t* name : {L"\\Fonts\\segoeuib.ttf", L"\\Fonts\\segoeui.ttf", L"\\Fonts\\arialbd.ttf"}) {
        std::wstring path = std::wstring(dir) + name;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        char utf8[MAX_PATH * 3] = {};
        WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, utf8, sizeof utf8, nullptr, nullptr);
        ImFontConfig config;
        config.OversampleH = 2;
        g_font = io.Fonts->AddFontFromFileTTF(utf8, 64.0f, &config);
        if (g_font) return;
    }
}

}  // namespace links
