#pragma once
// The port's own windows' font (DIRECT CONNECT, the quick menu): Segoe UI Bold from Windows, else Segoe UI, else Arial
// Bold, at 64 px to be scaled down sharp. ImGui's default stays the atlas's first font, the FPS counter's.
struct ImFont;

namespace links {

// GameInfo::window, before the renderer builds its font atlas
void LoadUiFont();
// that font; null when Windows had none of them (ImGui's default is used then)
ImFont* UiFont();

}  // namespace links
