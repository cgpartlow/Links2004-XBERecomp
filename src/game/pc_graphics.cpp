// PC GRAPHICS: a second item in the Graphics Options menu, under BRIGHTNESS, and the screen it opens, with the port's
// own display settings in the game's own menu style.
//
// The menus are Lua scenes in the GuiPackages .rui archives (zips). Nothing on disk changes: the game reads its own
// files and these hooks change what it loaded, in memory.
//
// The item:
// - XGraphicsOptionsMenu.scene lists one item, BRIGHTNESS; PatchLuaFile (0x43A01 in Rui_ReadLuaFile 0x438C0, just
//   before a file runs) swaps in Lua source with PC GRAPHICS and EXPERIMENTAL added (the game's Lua 5.0 has its parser).
// - Its layout (XGraphicsOptionsMenu.sceneData) has four list slots, but only the first holds a button (a MainButton_TMP
//   template node). LuaFileRan (0x43A06, just after a file ran) puts one into the second and third slots before the
//   game builds the screen from SceneData (0x49208).
// - The sceneData also carries the two binary tables the list control is wired from: NodeHierarchy names the nodes (a
//   control's id is its place there, from 1) and ControlData is the control tree, where the second and third slots
//   ("Button2", "Button3") had no children. PatchLuaFile adds each template's two nodes and hangs them under its
//   button, as Button1 has them. Both tables are string constants in the bytecode, edited before it runs.
// - Left and Right (0x93B10, 0x93B60) change the brightness only on BRIGHTNESS.
//
// The screen: the Xbox Live player feedback menu (XLiveFeedbackMenu, menu 0x13, in the Base package so it also works
// from the pause menu), which this port can never reach since Xbox Live is gone. It is a list of eight buttons; each
// becomes a setting whose button reads "NAME: VALUE". There are twelve settings: the list scrolls when it has more items
// than buttons (0x5C200 keeps the first one shown, +0x20, within the buttons, +0x40, of the selection, +0x24).
// - Its scene becomes this file's Lua (twelve items, Left and Right), its title and the two section headings of its
//   layout point at this file's strings, and its init (0xA8270) keeps the LIVE and XSN logos hidden (0xA84B7). The
//   layout left room for the second heading between its second and third buttons: they are spaced evenly instead.
// - The menus' font has no '%' (it draws a '/'), so no setting's text uses one.
// - With more settings than buttons, the screen gets the game's scroll indicator: the up and down arrows of the
//   Arrows_TMP template, as the soundtrack list has them. A list drives the control named "scrollbar" among its
//   children (0x5D072), lighting the arrows by where the list is scrolled (0x5C100). The template goes under the
//   layout's ItemList (LuaFileRan) and two controls under its ItemList control, as XSoundTrackMenu wires them: one for
//   the template ("Arrows_TMP:Menu") and "scrollbar" for its arrows ("Arrows_TMP:Arrows"). The arrows sit at the
//   right edge of the panel, above the first button and below the last.
// - In a round the screen's init (0xA8270) shows its "IngameBackground" control, which in this layout is a shaded panel
//   behind the list and title (Color_BackgroundShape: TitleBackgroundColor, BackgroundColorShape1 and the hidden
//   BackgroundColorShape2, at 40% opacity). The Graphics Options menu shows black widescreen bars there instead. The
//   three meshes become those bars as the layout's geometry loads (Scenes/XLiveFeedbackMenu.geom in Base.ruirr, read
//   by 0x265C50, each mesh by 0x2656F0): the faint tint over the middle, the top bar and the bottom bar, in the bars'
//   own colour. LuaFileRan puts the nodes at the menu's origin, opaque, all three shown. Outside a round the control
//   stays hidden, so the screen looks as it did.
// - SKIP INTRO leaves the boot's legal screens, logo movies and intro out (SkipLegalScreen and the hooks after it).
// - A on PC GRAPHICS pushes it by name (0x7C310 on the menu manager 0x453010), as the Options menu opens its pages.
// - Left, Right and A change the selected setting; its button text is a string table entry rewritten each time
//   (PCGfx1 to PCGfx12). The list built its items once, each keeping its text (item +0x0C, from the string table by
//   0x43BA0's way): the selected item gets its text again, then the list shows its items again (0x5C200).
//   The entries are made with room for kRowChars characters when the table loads, and rewritten in place: the menus'
//   heap (0x6C2050, about 1 MB) has no room for a second copy of the 150 KB table, and an allocation it cannot give
//   reboots the game (its out of memory handler).
// - B goes back as the menu always did. Every change is saved to the settings file at once.
//
// EXPERIMENTAL: a third item in the Graphics Options menu, under PC GRAPHICS, wired into the layout's third slot as
// the second is, and the screen it opens, for features still being tried out. The screen is the Xbox Live status menu
// (XLiveStatusMenu, menu 0x11, also in the Base package), a list of two buttons beside two status grids:
// - Its scene becomes this file's (two items, A, Left and Right), its title points at this file's string, and its in-round
//   background becomes the widescreen bars as PC GRAPHICS's does (its BackgroundColorShape1 holds both bars).
// - Its init (0xAB730) asked Xbox Live for the player's state; it is replaced with the parts a menu needs: the base init
//   (0x8C070), the in-round background, the player's controller (+0x30, which its B method checks) and the button list
//   (+0x28). The two status grids (Online, VoiceMask) and the LIVE logos stay hidden. Its Left and Right (0xAB590,
//   0xAB620) changed the Live state: they do nothing, and the glue changes the selected setting instead.
// - UPSCALED UI turns the upscaled menu textures (textures\generated\ui, the runtime's generated texture pack) on and
//   off. BUILD UPSCALED UI asks first, then closes the game and starts this exe as the upscaler (ui_upscale.cpp), which
//   starts the game again when it is done.
//
// Quitting from the main menu: XMainMenu.scene has no B, and the main menu's B method (vtable 0x3E12E0, +0x0C) is the
// shared empty one. The scene gets B and Back ("OnHitBButton()", the glue 0x791E0), whose override asks with the
// game's own message box (the menu manager's 0x7DCE0, as the pause menu asks before quitting a round) and closes the
// window on YES, as Alt+F4 does. The main menu's help lines get "B QUIT".
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "app/app.h"
#include "common/ui_upscale.h"
#include "core/log.h"
#include "imgui.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace links {
bool g_fps_counter = false;    // settings file fps_counter (game_info.cpp lists it), drawn by DrawPcOverlay
bool g_original_fps = false;   // settings file original_frame_rate: rounds at the Xbox's 30 FPS (UnlockGameFps)
uint32_t g_lod_percent = 100;  // settings file lod_distance: the objects' detail distances, 100 as the game has them
uint32_t g_grass_percent = 100;  // settings file grass_distance: how far the 3D grass reaches, 100 as the game has it
bool g_skip_intro = false;  // settings file skip_intro: boot goes from the loading screen straight to PRESS START
}

namespace {

constexpr uint32_t kMainMenuVtable = 0x003E12E0;      // XMainMenu, set by its constructor 0x92C60
constexpr uint32_t kMessageBox = 0x0007DCE0;          // thiscall on the menu manager, see AskToQuit
constexpr uint32_t kMessageYesNo = 11;                // its kind with the "A YES B NO" prompt (MessagePromptYesNo)
constexpr uint32_t kGraphicsMenuVtable = 0x003E1570;  // XGraphicsOptionsMenu, set by its constructor 0x93BF0
constexpr uint32_t kFeedbackVtable = 0x003E2EB8;      // XLiveFeedbackMenu, set by its constructor 0xA81A0
constexpr uint32_t kFeedbackName = 0x003E2F3C;        // "XLiveFeedbackMenu" in the image
constexpr uint32_t kMenuManager = 0x00453010;
constexpr uint32_t kPushMenu = 0x0007C310;            // thiscall (const CString& name), the Options menu's way
constexpr uint32_t kCStringFromText = 0x00031D0E;     // thiscall CString(const char*)
constexpr uint32_t kCStringFree = 0x00031A80;         // thiscall ~CString()
constexpr uint32_t kListSetSelection = 0x0005C200;    // thiscall (selection, 0) on a button list: shows the items again
constexpr uint32_t kItemSetText = 0x0005CFE0;         // thiscall (const CStringW&) on a list item: its text, at +0x0C
constexpr uint32_t kGetText = 0x00043BA0;             // thiscall (CStringW* out, const CString& id, 0) on the GUI
constexpr uint32_t kGui = 0x00452CEC;                 // the GUI object's pointer
constexpr uint32_t kGameAlloc = 0x0006E0C0;           // cdecl (size, 0, file, line)
constexpr uint32_t kGameFree = 0x0006DF00;            // cdecl (block, 0, 0, 0)
constexpr uint32_t kStringTableSource = 0x003DD060;   // "...\RGuiStringTable.cpp", the tag the table's own buffer has
constexpr uint32_t kPcGraphicsItem = 1;               // index in the graphics menu's list, BRIGHTNESS is 0
constexpr uint32_t kExperimentalItem = 2;             // ...and EXPERIMENTAL's
constexpr uint32_t kRunLua = 0x00111460;              // thiscall (bytes, size, name) on the Lua state: load and run
constexpr uint32_t kStatusVtable = 0x003E3350;        // XLiveStatusMenu, set by its constructor 0xAB6C0
constexpr uint32_t kStatusName = 0x003E2958;          // "XLiveStatusMenu" in the image
constexpr uint32_t kMenuEntered = 0x0008BCA0;         // thiscall () on a menu, then the manager's 0x7A7F0 takes it
constexpr uint32_t kManagerEntered = 0x0007A7F0;      // thiscall (that) on the menu manager, as every init starts
constexpr uint32_t kMenuBaseInit = 0x0008C070;        // thiscall (scene, arg) on a menu: builds its controls
constexpr uint32_t kFindControl = 0x000482F0;         // thiscall (out, const CString& name, 1) on scene + 4's object
constexpr uint32_t kControlList = 0x0004C280;         // cdecl (control's node): the list it is
constexpr uint32_t kInRound = 0x004BE73F;             // byte, set while a round is on (the menus' in-round background)
constexpr uint32_t kButtonListName = 0x003E1470;      // "ButtonList"
constexpr uint32_t kIngameBackgroundName = 0x003E2CD8;  // "IngameBackground"
constexpr uint32_t kLiveLogosName = 0x003E224C;       // "PreGameLiveLogos"
constexpr uint32_t kOnlineName = 0x003E3444;          // "Online", a status grid
constexpr uint32_t kVoiceMaskName = 0x003E3438;       // "VoiceMask", the other

// The scene as the original builds it (Scenes.XGraphicsOptionsMenu, decompiled), with the second and third items and A
// added
const char kGraphicsScene[] = R"lua(Scenes.XGraphicsOptionsMenu = {
	Controls = {
		{
			DefaultInput = "ListProcessInput",
			Down = "ListSelNext()",
			Up = "ListSelPrevious()",
			Items = {
				AButton = "OnHitAButton()",
				BButton = "OnHitBButton()",
				Back = "OnHitBButton()",
				Start = "OnHitAButton()",
				Enabled = { true, true, true },
				HelpIDs = { "HelpGraphicsOptions", "HelpPCGraphics", "HelpPCExperimental" },
				Left = "OnHitLeft()",
				Right = "OnHitRight()",
				TextIDs = { "GraphicsOptionsButton1", "PCGraphicsButton", "PCExperimentalButton" },
			},
		},
	},
	DefaultAction = "$SetupMenu",
}
)lua";

// The EXPERIMENTAL screen's scene, in place of XLiveStatusMenu's (decompiled: B, Back, Left and Right on the control,
// two items) with this screen's items and A
const char kExperimentalScene[] = R"lua(Scenes.XLiveStatusMenu = {
	Controls = {
		{
			BButton = "OnHitBButton()",
			Back = "OnHitBButton()",
			DefaultInput = "ListProcessInput",
			Down = "ListSelNext()",
			Up = "ListSelPrevious()",
			Left = "OnHitLeft()",
			Right = "OnHitRight()",
			Items = {
				AButton = "OnHitAButton()",
				Start = "OnHitAButton()",
				Enabled = { true, true },
				HelpIDs = { "HelpPCExp1", "HelpPCExp2" },
				TextIDs = { "PCExp1", "PCExp2" },
			},
		},
	},
	DefaultAction = "$SetupMenu",
}
)lua";

// the scroll indicator's place, in the ItemList's frame (the buttons' x is -102, the first at y 87, the last -122)
constexpr float kArrowX = 232.0f, kArrowTop = 108.0f, kArrowBottom = -143.0f, kArrowScale = 0.75f;

// floats as a Lua string of their bytes, for a node's LPM (its position, then a 3 x 3 matrix)
std::string LuaBytes(std::initializer_list<float> values) {
    std::string out = "\"";
    for (float v : values) {
        uint8_t b[4];
        std::memcpy(b, &v, 4);
        char e[8];
        for (uint8_t c : b) {
            std::snprintf(e, sizeof e, "\\%03u", c);
            out += e;
        }
    }
    return out + "\"";
}

// Run after the PC GRAPHICS and EXPERIMENTAL screens' sceneData: the in-round background (Color_BackgroundShape, a
// child of Graph.Children[2], the menu) at the menu's origin and opaque, its meshes, which PcGeometryMesh made the
// widescreen bars, at the origin too and all shown. No function calls (see SlotPatch).
const char kBackgroundPatch[] = R"lua(
do
	local graph = SceneData and SceneData.Graph
	local menu = graph and graph.Children and graph.Children[2]
	local shade = nil
	if menu and menu.Children then
		for i = 1, 64 do
			local c = menu.Children[i]
			if c and c.Name == "Color_BackgroundShape" then shade = c end
		end
	end
	if shade and shade.Children then
		shade.LPM = "\000\000\000\000\000\000\000\000\000\000\000\000"
		shade.Color = { 1, 1, 1, 1 }
		for i = 1, 3 do
			local c = shade.Children[i]
			if c then
				c.Flags = 2
				c.LPM = shade.LPM
				c.Visible = true
			end
		end
	end
end
)lua";

// Run after the PC GRAPHICS screen's sceneData: the scroll indicator's template under the list ("ItemList", a child
// of the menu), its two arrows moved and scaled as XSoundTrackMenu.sceneData does it (the bottom one turned upside
// down), the template added to the scene's list of them. Then the in-round background (kBackgroundPatch).
std::string PcLayoutPatch() {
    auto attribute = [](const char* path, float y, float turn) {
        char head[256];
        std::snprintf(head, sizeof head,
                      "\t\t\t[\"%s\"] = { Flags = 6, scaleX = %.2f, scaleY = %.2f, scaleZ = %.2f, translateX = %.1f, "
                      "translateY = %.1f, translateZ = 0, LPM = ",
                      path, kArrowScale, kArrowScale, kArrowScale, kArrowX, y);
        float s = kArrowScale * turn;
        return std::string(head) + LuaBytes({kArrowX, y, 0, s, 0, 0, 0, s, 0, 0, 0, kArrowScale}) + " },\n";
    };
    return std::string(R"lua(
local graph = SceneData and SceneData.Graph
local menu = graph and graph.Children and graph.Children[2]
local list = nil
if menu and menu.Children then
	for i = 1, 64 do
		local c = menu.Children[i]
		if c and c.Name == "ItemList" and c.Type == "List" then list = c end
	end
end
if list and list.Children and list.Children[1] and not list.Children[2] then
	list.Children[2] = {
		ChangedAttributes = {
)lua") + attribute("Menu|Arrows|TopArrow|OrangeMenuArrow", kArrowTop, 1.0f) +
           attribute("Menu|Arrows|BottomArrow|OrangeMenuArrow1", kArrowBottom, -1.0f) + R"lua(		},
		ClassType = "",
		Flags = 0,
		HasRotation = false,
		HasShear = false,
		HasTranslation = false,
		IsAnimated = false,
		Name = "Arrows_TMP:Menu",
		TemplateName = "Arrows_TMP",
		Type = "Template",
	}
	local templates = SceneData.Templates
	if templates and templates[1] and not templates[2] then templates[2] = "Arrows_TMP" end
	local types = SceneData.NodeTypes and SceneData.NodeTypes[0]
	if types and types.Template then types.Template = types.Template + 1 end
end
)lua" + kBackgroundPatch;
}

// The main menu's scene (Scenes.XMainMenu, decompiled) with B and Back added
const char kMainScene[] = R"lua(Scenes.XMainMenu = {
	Controls = {
		{
			DefaultInput = "ListProcessInput",
			Down = "ListSelNext()",
			Up = "ListSelPrevious()",
			Items = {
				AButton = "OnHitAButton()",
				AButtonDisabled = "$Disabled",
				BButton = "OnHitBButton()",
				Back = "OnHitBButton()",
				Enabled = { true, true, true, true, true, true, true },
				HelpIDs = { "HelpMainButton2", "HelpMainButton3", "HelpMainButton4", "HelpMainButton5", "HelpMainButton6",
				            "HelpMainButton7", "HelpMainButton8" },
				Start = "OnHitAButton()",
				StartDisabled = "$Disabled",
				TextIDs = { "MainButton2", "MainButton3", "MainButton4", "MainButton5", "MainButton6", "MainButton7",
				            "MainButton8" },
			},
		},
	},
	DefaultAction = "$SetupMenu",
	SceneOrder = {
		{ SceneAction = "$SetupMenu", SceneName = "XBackgroundMenu2" },
		{ SceneName = "XMainMenu" },
	},
}
)lua";

// Run after the graphics menu's sceneData: the second and third list slots ("Item2", "Item3") get the button template
// the first one has, the way XSoundOptionsMenu's slots each have theirs. Their place in SceneData.Graph is fixed by the
// game's file (Graph.Children[2] the menu, its Children[4] ItemList, Children[1] items, Children[2] Item2 and
// Children[3] Item3), and every step is checked. Nothing here may fail: the game's Lua has no base library (no
// type()), and a Lua error unwinds with longjmp, which a recompiled game cannot take (0x11AFC0 back into the setjmp at
// 0x11B023).
std::string SlotPatch(int slot) {
    std::string n = std::to_string(slot);
    return R"lua(
do
	local graph = SceneData and SceneData.Graph
	local menu = graph and graph.Children and graph.Children[2]
	local list = menu and menu.Children and menu.Children[4]
	local items = list and list.Children and list.Children[1]
	local slot = items and items.Children and items.Children[)lua" +
           n + R"lua(]
	if slot and slot.Name == "Item)lua" + n +
           R"lua(" and slot.Children and slot.Children[1] and not slot.Children[1].Children then
		slot.Children[1].Children = {
			{
				ChangedAttributes = {},
				ClassType = "",
				Flags = 0,
				HasRotation = false,
				HasShear = false,
				HasTranslation = false,
				IsAnimated = false,
				Name = "MainButton_TMP)lua" +
           n + R"lua(:Menu",
				TemplateName = "MainButton_TMP",
				Type = "Template",
			},
		}
	end
end
)lua";
}
std::string GraphicsLayoutPatch() { return SlotPatch(2) + SlotPatch(3); }

// The PC GRAPHICS screen's scene, in place of XLiveFeedbackMenu's: thirteen settings, Left and Right change them
const char kPcScene[] = R"lua(Scenes.XLiveFeedbackMenu = {
	Controls = {
		{
			DefaultInput = "ListProcessInput",
			Down = "ListSelNext()",
			Up = "ListSelPrevious()",
			Items = {
				AButton = "OnHitAButton()",
				BButton = "OnHitBButton()",
				Back = "OnHitBButton()",
				Left = "OnHitLeft()",
				Right = "OnHitRight()",
				Enabled = { true, true, true, true, true, true, true, true, true, true, true, true, true },
				HelpIDs = "HelpPCGfx",
				TextIDs = { "PCGfx1", "PCGfx2", "PCGfx3", "PCGfx4", "PCGfx5", "PCGfx6", "PCGfx7", "PCGfx8", "PCGfx9",
				            "PCGfx10", "PCGfx11", "PCGfx12", "PCGfx13" },
			},
		},
	},
	DefaultAction = "$SetupMenu",
}
)lua";

// XLiveFeedbackMenu.sceneData's text ids that become this screen's: its title and the two section headings
const std::pair<const char*, const char*> kPcLayoutText[] = {
    {"LiveFeebackTitle", "PCGraphicsTitle"},
    {"LiveFeebackComplaints", "PCGfxBlank"},
    {"LiveFeedbackGoodFeedbackText", "PCGfxBlank"},
};
// XLiveStatusMenu.sceneData's: its title (both of its title nodes use the one constant); the grids' texts stay, hidden
const std::pair<const char*, const char*> kExperimentalLayoutText[] = {
    {"LiveStatusTitle", "PCExpTitle"},
};

// XLiveFeedbackMenu.sceneData's button slots, Item1 to Item7: Color nodes whose LPM, a 12 byte string constant, is
// their x, y and z. x is -102 for all of them; their y as the file has it. The second and third are 52 apart where the
// rest are about 31, the room for a heading. Item1 and Item7 stay where they are and the five between are spaced
// evenly (ItemLast, below them, is not one of the list's buttons).
constexpr uint32_t kSlotX = 0xC2CC0000;  // -102.0
const uint32_t kSlotY[7] = {0x42ADFFFB, 0x425A046E, 0x3FE1531E, 0xC1F37BB1, 0xC2753783, 0xC2B6F79B, 0xC2F4B0E6};

bool EndsWithNoCase(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++) {
        char a = s[s.size() - n + i], b = suffix[i];
        if (a == '\\') a = '/';
        if (std::tolower(static_cast<unsigned char>(a)) != std::tolower(static_cast<unsigned char>(b))) return false;
    }
    return true;
}

uint32_t GameAlloc(X86Context& ctx, uint32_t size) {
    return ::kernel::CallGuest(ctx, kGameAlloc, {size, 0, kStringTableSource, 0x3A});
}
void GameFree(X86Context& ctx, uint32_t block) {
    if (block) ::kernel::CallGuest(ctx, kGameFree, {block, 0, 0, 0});
}

// The sceneData bytecode: string constants are a type byte 4, a 32 bit length counting a closing 0, the bytes and the
// 0. A dump has no offsets, so one can change length in place. The one constant edit() accepts is replaced.
template <class Edit>
bool EditStringConstant(std::vector<uint8_t>& code, Edit edit) {
    size_t found = 0, at = 0, len = 0;
    std::string result;
    for (size_t p = 22; p + 5 <= code.size(); p++) {
        if (code[p] != 4) continue;
        uint32_t n;
        std::memcpy(&n, &code[p + 1], 4);
        if (n < 2 || n > code.size() - p - 5 || code[p + 5 + n - 1] != 0) continue;
        std::string out;
        if (!edit(std::string(reinterpret_cast<const char*>(&code[p + 5]), n - 1), out)) continue;
        found++;
        at = p;
        len = n;
        result = std::move(out);
    }
    if (found != 1) return false;
    std::vector<uint8_t> bytes(5 + result.size() + 1);
    bytes[0] = 4;
    uint32_t n = uint32_t(result.size() + 1);
    std::memcpy(&bytes[1], &n, 4);
    std::memcpy(&bytes[5], result.data(), result.size());
    code.erase(code.begin() + at, code.begin() + at + 5 + len);
    code.insert(code.begin() + at, bytes.begin(), bytes.end());
    return true;
}

// the button slots between the first and last moved to even spacing; the count moved
int SpaceSlots(std::vector<uint8_t>& code) {
    auto lpm = [](uint32_t y) {
        std::string s(12, '\0');
        std::memcpy(&s[0], &kSlotX, 4);
        std::memcpy(&s[4], &y, 4);
        return s;
    };
    float top, bottom;
    std::memcpy(&top, &kSlotY[0], 4);
    std::memcpy(&bottom, &kSlotY[6], 4);
    int moved = 0;
    for (int i = 1; i < 6; i++) {
        float y = top + (bottom - top) * float(i) / 6.0f;
        uint32_t bits;
        std::memcpy(&bits, &y, 4);
        std::string from = lpm(kSlotY[i]), to = lpm(bits);
        moved += EditStringConstant(code, [&](const std::string& s, std::string& out) {
            if (s != from) return false;
            out = to;
            return true;
        });
    }
    return moved;
}

uint16_t Get16(const std::string& s, size_t at) { return uint16_t(uint8_t(s[at]) | uint8_t(s[at + 1]) << 8); }
void Put16(std::string& s, size_t at, uint16_t v) {
    s[at] = char(v & 0xFF);
    s[at + 1] = char(v >> 8);
}
std::string Le16(uint16_t v) { return std::string{char(v & 0xFF), char(v >> 8)}; }
std::string Named(const std::string& name) { return Le16(uint16_t(name.size())) + name; }

// a graphics menu slot's button template node (SlotPatch) and its text node
std::string SlotTemplate(int slot) { return "MainButton_TMP" + std::to_string(slot) + ":Menu"; }
std::string SlotText(int slot) { return "MainButton_TMP" + std::to_string(slot) + ":MainMenuTMP_TextTop"; }

// NodeHierarchy: two 16 bit counts, then each node as a 16 bit length, the name and 16 bits of 0. Appends a slot's
// two nodes; their ids are the old count + 1 and + 2.
bool AddSlotNodes(std::vector<uint8_t>& code, int slot, uint16_t& first_id) {
    return EditStringConstant(code, [&](const std::string& s, std::string& out) {
        if (s.size() < 4 || s.find(Named("MainButton_TMP1:Menu")) == std::string::npos ||
            s.find(Named(SlotTemplate(slot))) != std::string::npos)
            return false;
        uint16_t count = Get16(s, 0);
        if (Get16(s, 2) != count) return false;
        out = s + Named(SlotTemplate(slot)) + std::string(2, '\0') + Named(SlotText(slot)) + std::string(2, '\0');
        Put16(out, 0, uint16_t(count + 2));
        Put16(out, 2, uint16_t(count + 2));
        first_id = uint16_t(count + 1);
        return true;
    });
}

// ControlData: a 16 bit control count and top level count, then each control as its name (16 bit length and text),
// 0xFFFF or its class, its 16 bit id and its child count, its children after it. ButtonN (2 or 3), which has none,
// gets Button1's two children.
bool WireSlotControl(std::vector<uint8_t>& code, int slot, uint16_t first_id) {
    return EditStringConstant(code, [&](const std::string& s, std::string& out) {
        std::string key = Named("Button" + std::to_string(slot)) + "\xFF\xFF";
        size_t at = s.find(key);
        if (s.size() < 4 || at == std::string::npos || s.find(Named("ButtonList")) == std::string::npos) return false;
        size_t kids = at + key.size() + 2;
        if (kids + 2 > s.size() || Get16(s, kids) != 0) return false;
        out = s;
        Put16(out, kids, 2);
        std::string children = Named("MainButton_TMP") + "\xFF\xFF" + Le16(first_id) + Le16(0) + Named("text") +
                               "\xFF\xFF" + Le16(uint16_t(first_id + 1)) + Le16(0);
        out.insert(kids + 2, children);
        Put16(out, 0, uint16_t(Get16(s, 0) + 2));
        return true;
    });
}

// The PC GRAPHICS screen's NodeHierarchy gets the scroll indicator's two nodes, ids the old count + 1 and + 2
bool AddArrowNodes(std::vector<uint8_t>& code, uint16_t& first_id) {
    return EditStringConstant(code, [&](const std::string& s, std::string& out) {
        if (s.size() < 4 || s.find(Named("MainButton_TMP15:Menu")) == std::string::npos ||
            s.find(Named("Arrows_TMP:Menu")) != std::string::npos)
            return false;
        uint16_t count = Get16(s, 0);
        if (Get16(s, 2) != count) return false;
        out = s + Named("Arrows_TMP:Menu") + std::string(2, '\0') + Named("Arrows_TMP:Arrows") + std::string(2, '\0');
        Put16(out, 0, uint16_t(count + 2));
        Put16(out, 2, uint16_t(count + 2));
        first_id = uint16_t(count + 1);
        return true;
    });
}

// ...and its ItemList control (class 1) two more children: the template and the "scrollbar" the list drives
bool WireArrows(std::vector<uint8_t>& code, uint16_t first_id) {
    return EditStringConstant(code, [&](const std::string& s, std::string& out) {
        std::string key = Named("ItemList") + std::string("\x01\x00", 2);
        size_t at = s.find(key);
        if (s.size() < 4 || at == std::string::npos || s.find(Named("scrollbar")) != std::string::npos) return false;
        size_t kids = at + key.size() + 2;
        if (kids + 2 > s.size()) return false;
        out = s;
        Put16(out, kids, uint16_t(Get16(s, kids) + 2));
        std::string children = Named("Arrows_TMP") + "\xFF\xFF" + Le16(first_id) + Le16(0) + Named("scrollbar") +
                               "\xFF\xFF" + Le16(uint16_t(first_id + 1)) + Le16(0);
        out.insert(kids + 2, children);
        Put16(out, 0, uint16_t(Get16(s, 0) + 2));
        return true;
    });
}

// Swaps the bytes Rui_ReadLuaFile is about to run ([esp], [esp + 4], and edi, which it frees after) for new ones
bool ReplaceLuaFile(X86Context& ctx, const void* bytes, uint32_t size) {
    uint32_t mem = GameAlloc(ctx, size);
    if (!mem) return false;
    std::memcpy(::kernel::HostPtr(mem), bytes, size);
    GameFree(ctx, ctx.edi);
    ctx.edi = mem;
    ::kernel::W32(ctx.esp, mem);
    ::kernel::W32(ctx.esp + 4, size);
    return true;
}

// ---- the settings ----

std::string Upper(std::string s) {
    for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// the game's monitor's modes (app::GameMonitor), distinct sizes from 640x480 up, smallest first
std::vector<std::pair<uint32_t, uint32_t>> Resolutions() {
    std::vector<std::pair<uint32_t, uint32_t>> out;
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    HMONITOR mon = static_cast<HMONITOR>(app::GameMonitor());
    if (!GetMonitorInfoW(mon, &mi)) return out;
    DEVMODEW m{};
    m.dmSize = sizeof(m);
    for (DWORD i = 0; EnumDisplaySettingsW(mi.szDevice, i, &m); i++)
        if (m.dmPelsWidth >= 640 && m.dmPelsHeight >= 480) out.emplace_back(m.dmPelsWidth, m.dmPelsHeight);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

template <class T>
size_t IndexOf(const std::vector<T>& list, const T& v) {
    auto it = std::find(list.begin(), list.end(), v);
    return it == list.end() ? 0 : size_t(it - list.begin());
}
template <class T>
T Step(const std::vector<T>& list, const T& now, int dir) {
    size_t i = IndexOf(list, now), n = list.size();
    return list[(i + n + size_t(dir < 0 ? n - 1 : 1)) % n];
}

const std::vector<uint32_t> kRenderPercents = {50, 75, 100, 125, 150, 175, 200};
const std::vector<uint32_t> kFpsCaps = {30, 60, app::kFpsCapRefresh, 120, 144, 165, 240, 0};
const std::vector<uint32_t> kAniso = {0, 2, 4, 8, 16};
const std::vector<uint32_t> kOverscan = {0, 5, 10, 15, 20, 25, 30, 40, 50};
const std::vector<uint32_t> kLodPercents = {100, 150, 200, 300, 400};
const std::vector<uint32_t> kGrassPercents = {100, 150, 200, 300};  // GrowGrassIndices makes room for the last

// a percentage as a multiple: 150 is "1.5X", 75 "0.75X", 200 "2X"
std::string Times(uint32_t percent) {
    char b[16];
    if (percent % 100 == 0) std::snprintf(b, sizeof b, "%uX", percent / 100);
    else if (percent % 10 == 0) std::snprintf(b, sizeof b, "%u.%uX", percent / 100, percent % 100 / 10);
    else std::snprintf(b, sizeof b, "%u.%02uX", percent / 100, percent % 100);
    return std::string(b);
}

// A setting: "LABEL: VALUE" on its button (just the label when the value is empty). Left, Right and A step it, or,
// when it has `press`, A presses it and Left and Right do nothing.
struct Row {
    const char* label;
    std::string (*value)();
    void (*step)(int dir);
    void (*press)(X86Context& ctx) = nullptr;
};

// the game's monitor among the attached ones, -1 when it is none of them
int MonitorIndex(const std::vector<app::Monitor>& list) {
    void* now = app::GameMonitor();
    for (size_t i = 0; i < list.size(); i++)
        if (list[i].handle == now) return int(i);
    return -1;
}

const Row kRows[] = {
    // numbered left to right as Windows places them; a new monitor starts at its desktop resolution
    {"MONITOR",
     [] {
         std::vector<app::Monitor> list = app::Monitors();
         int i = MonitorIndex(list);
         if (i < 0) return std::string("PRIMARY");
         char b[40];
         std::snprintf(b, sizeof b, "%d (%ux%u)", i + 1, list[size_t(i)].width, list[size_t(i)].height);
         return std::string(b);
     },
     [](int dir) {
         std::vector<app::Monitor> list = app::Monitors();
         if (list.size() < 2) return;
         int n = int(list.size()), i = std::max(MonitorIndex(list), 0);
         const app::Monitor& next = list[size_t((i + (dir < 0 ? n - 1 : 1)) % n)];
         {
             std::lock_guard<std::mutex> guard(app::MonitorLock());
             app::Get().monitor = next.device;
         }
         app::Get().width = app::Get().height = 0;  // its desktop's
         app::ApplyDisplay();
     }},
    {"DISPLAY",
     [] {
         const char* m[] = {"FULLSCREEN", "BORDERLESS", "WINDOWED"};
         return std::string(m[uint32_t(app::Get().display_mode) % 3]);
     },
     [](int dir) {
         uint32_t m = uint32_t(app::Get().display_mode);
         app::Get().display_mode = app::DisplayMode((m + (dir < 0 ? 2 : 1)) % 3);
         app::ApplyDisplay();
     }},
    {"RESOLUTION",
     [] {
         const app::Settings& s = app::Get();
         if (!s.width || !s.height) return std::string("DESKTOP");
         char b[32];
         std::snprintf(b, sizeof b, "%ux%u", s.width, s.height);
         return std::string(b);
     },
     [](int dir) {
         std::vector<std::pair<uint32_t, uint32_t>> list = Resolutions();
         list.insert(list.begin(), {0, 0});  // the desktop's
         app::Settings& s = app::Get();
         std::pair<uint32_t, uint32_t> now{s.width, s.height};
         if (std::find(list.begin(), list.end(), now) == list.end()) now = {0, 0};
         std::tie(s.width, s.height) = Step(list, now, dir);
         app::ApplyDisplay();
     }},
    // ORIGINAL is the Xbox's: menus at 60, rounds at 30; every other choice is the cap for both
    {"FRAME RATE",
     [] {
         uint32_t c = app::Get().fps_cap;
         if (links::g_original_fps) return std::string("ORIGINAL");
         if (c == 0) return std::string("UNLIMITED");
         if (c == app::kFpsCapRefresh) return std::string("MONITOR");
         return std::to_string(c) + " FPS";
     },
     [](int dir) {
         // the choices in order, ORIGINAL first as cap 0xFFFFFFFF
         std::vector<uint32_t> list{0xFFFFFFFFu};
         list.insert(list.end(), kFpsCaps.begin(), kFpsCaps.end());
         uint32_t now = links::g_original_fps ? 0xFFFFFFFFu : app::Get().fps_cap;
         uint32_t next = Step(list, now, dir);
         links::g_original_fps = next == 0xFFFFFFFFu;
         app::Get().fps_cap = links::g_original_fps ? 60 : next;
     }},
    {"FPS COUNTER", [] { return std::string(links::g_fps_counter ? "ON" : "OFF"); },
     [](int) { links::g_fps_counter = !links::g_fps_counter; }},
    {"V-SYNC", [] { return std::string(app::Get().vsync ? "ON" : "OFF"); },
     [](int) { app::Get().vsync = !app::Get().vsync; }},
    {"OVERSCAN",
     [] {
         uint32_t o = app::Get().overscan;
         char b[16];
         std::snprintf(b, sizeof b, "%u.%u PERCENT", o / 10, o % 10);
         return o ? std::string(b) : std::string("OFF");
     },
     [](int dir) { app::Get().overscan = Step(kOverscan, app::Get().overscan, dir); }},
    {"RENDER SCALE",
     [] { return Times(app::Get().render_percent); },
     [](int dir) { app::Get().render_percent = Step(kRenderPercents, app::Get().render_percent, dir); }},
    // over the finished frame, in the renderer (gpu/aa.cpp): SMAA keeps text and fine detail sharper than FXAA
    {"ANTI-ALIASING",
     [] {
         const char* m[] = {"OFF", "FXAA", "SMAA"};
         return std::string(m[app::Get().anti_aliasing % 3]);
     },
     [](int dir) {
         uint32_t& a = app::Get().anti_aliasing;
         a = (a % 3 + (dir < 0 ? 2 : 1)) % 3;
     }},
    {"TEXTURE FILTER",
     [] {
         uint32_t a = app::Get().aniso;
         return a ? std::to_string(a) + "X" : std::string("STANDARD");
     },
     [](int dir) { app::Get().aniso = Step(kAniso, app::Get().aniso, dir); }},
    // how far away objects keep their detail and stay drawn (ScaleLodDistance)
    {"LOD DISTANCE",
     [] { return links::g_lod_percent == 100 ? std::string("ORIGINAL") : Times(links::g_lod_percent); },
     [](int dir) { links::g_lod_percent = Step(kLodPercents, links::g_lod_percent, dir); }},
    // how far ahead the 3D grass reaches (ScaleGrassSphere)
    {"GRASS DISTANCE",
     [] { return links::g_grass_percent == 100 ? std::string("ORIGINAL") : Times(links::g_grass_percent); },
     [](int dir) { links::g_grass_percent = Step(kGrassPercents, links::g_grass_percent, dir); }},
    // the legal screens, logo movies and intro left out from the next start (SkipLegalScreens and the rest)
    {"SKIP INTRO", [] { return std::string(links::g_skip_intro ? "ON" : "OFF"); },
     [](int) { links::g_skip_intro = !links::g_skip_intro; }},
};
constexpr uint32_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);

bool AskYesNo(X86Context& ctx, const char* title, const char* body);

// EXPERIMENTAL's settings
const Row kExperimentalRows[] = {
    // the upscaled menus, HUD and fonts on or off: the runtime's generated texture pack (gpu/texturedumper.cpp),
    // which notices the change at once
    {"UPSCALED UI",
     [] {
         if (!links::UpscaledUiBuilt()) return std::string("NOT BUILT");
         return std::string(app::Get().generated_textures ? "ON" : "OFF");
     },
     [](int) {
         if (links::UpscaledUiBuilt()) app::Get().generated_textures = !app::Get().generated_textures;
     }},
    // closes the game and starts the upscaler (ui_upscale.cpp), which starts the game again when it is done
    {"BUILD UPSCALED UI", [] { return std::string(); }, [](int) {},
     [](X86Context& ctx) {
         if (!AskYesNo(ctx, "PCBuildTitle", "PCBuildBody")) return;
         app::Save();
         if (!links::StartUiUpscale()) LOGE("menu: the UI upscaler did not start");
     }},
};
constexpr uint32_t kExperimentalRowCount = sizeof(kExperimentalRows) / sizeof(kExperimentalRows[0]);

// a screen of settings: its rows' string ids are the prefix and the row's number from 1
struct Screen {
    const char* prefix;
    const Row* rows;
    uint32_t count;
};
const Screen kPcScreen{"PCGfx", kRows, kRowCount};
const Screen kExperimentalScreen{"PCExp", kExperimentalRows, kExperimentalRowCount};
const Screen* const kScreens[] = {&kPcScreen, &kExperimentalScreen};

std::string RowString(const Row& row) {
    std::string value = row.value();
    return Upper(value.empty() ? std::string(row.label) : std::string(row.label) + ": " + value);
}
std::u16string RowText(const Screen& screen, uint32_t i) {
    std::string t = RowString(screen.rows[i]);
    return std::u16string(t.begin(), t.end());
}
std::string RowId(const Screen& screen, uint32_t i) { return screen.prefix + std::to_string(i + 1); }

// ---- the string table ----
// The file: a byte font count, the fonts (0x2C bytes each), the string count, an offset per string, the size of the
// strings, then the strings: a byte id length, the id and a 0, a 16 bit character count, the UTF-16 text, a byte font
// index and a float scale. The table object (0x57F00) keeps the file at +0x0C, the fonts at +0x10, the count at +0x14,
// the offsets at +0x18 and the strings at +0x1C; its lookup (0x57C90) is a binary search with strcmp.

uint32_t g_table = 0;  // the table object with the menus' strings, once loaded

struct Table {
    std::vector<uint8_t> head;  // the font count and fonts
    std::vector<std::pair<std::string, std::vector<uint8_t>>> entries;  // id, the whole entry
};

bool ReadTable(uint32_t buf, Table& t) {
    const uint8_t* d = static_cast<const uint8_t*>(::kernel::HostPtr(buf));
    uint32_t head = 1 + uint32_t(d[0]) * 0x2C, count, data_size;
    std::memcpy(&count, d + head, 4);
    if (count == 0 || count > 100000) return false;
    std::vector<uint32_t> offs(count);
    std::memcpy(offs.data(), d + head + 4, size_t(count) * 4);
    std::memcpy(&data_size, d + head + 4 + count * 4, 4);
    const uint8_t* data = d + head + 8 + count * 4;
    t.head.assign(d, d + head);
    t.entries.clear();
    t.entries.reserve(count + 16);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t end = i + 1 < count ? offs[i + 1] : data_size;
        if (offs[i] >= end || end > data_size) return false;
        const uint8_t* e = data + offs[i];
        t.entries.emplace_back(std::string(reinterpret_cast<const char*>(e + 1), e[0]),
                               std::vector<uint8_t>(e, data + end));
    }
    return true;
}

std::vector<std::pair<std::string, std::vector<uint8_t>>>::iterator Find(Table& t, const std::string& id) {
    return std::find_if(t.entries.begin(), t.entries.end(), [&](auto& x) { return x.first == id; });
}

constexpr uint32_t kRowChars = 40;  // room each row's entry has, so it can be rewritten in place

// an entry's bytes: the id length, id and 0, the character count, the text, then `tail` (font index and scale),
// zero padded to `size` bytes when that is more
std::vector<uint8_t> MakeEntry(const std::string& id, const std::u16string& text, const uint8_t* tail, size_t size) {
    std::vector<uint8_t> e;
    e.push_back(uint8_t(id.size()));
    e.insert(e.end(), id.begin(), id.end());
    e.push_back(0);
    e.push_back(uint8_t(text.size()));
    e.push_back(uint8_t(text.size() >> 8));
    for (char16_t c : text) {
        e.push_back(uint8_t(c));
        e.push_back(uint8_t(c >> 8));
    }
    e.insert(e.end(), tail, tail + 5);
    if (e.size() < size) e.resize(size, 0);
    return e;
}
size_t RowEntrySize(const std::string& id) { return 1 + id.size() + 1 + 2 + 2 * kRowChars + 5; }

// sets id's text, adding the entry with the font and scale of `like` when it is not there yet; `size` pads it
void SetString(Table& t, const std::string& id, const std::u16string& text, const char* like, size_t size = 0) {
    // the font index and scale follow the text, wherever padding ends the entry
    auto tail_of = [](const std::vector<uint8_t>& e) {
        size_t at = 1 + e[0] + 1, chars = e[at] | e[at + 1] << 8;
        return std::vector<uint8_t>(e.begin() + long(at + 2 + 2 * chars), e.begin() + long(at + 7 + 2 * chars));
    };
    auto at = Find(t, id);
    std::vector<uint8_t> tail;
    if (at != t.entries.end()) {
        tail = tail_of(at->second);
    } else {
        auto l = Find(t, like);
        if (l == t.entries.end()) return;
        tail = tail_of(l->second);
    }
    std::vector<uint8_t> e = MakeEntry(id, text, tail.data(), size);
    if (at != t.entries.end()) at->second = std::move(e);
    else t.entries.emplace_back(id, std::move(e));
}

std::vector<uint8_t> WriteTable(Table& t) {
    std::sort(t.entries.begin(), t.entries.end(),
              [](auto& a, auto& b) { return std::strcmp(a.first.c_str(), b.first.c_str()) < 0; });
    std::vector<uint8_t> out = t.head;
    auto put32 = [&](uint32_t v) {
        out.insert(out.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 4);
    };
    put32(uint32_t(t.entries.size()));
    uint32_t at = 0;
    for (auto& x : t.entries) {
        put32(at);
        at += uint32_t(x.second.size());
    }
    put32(at);
    for (auto& x : t.entries) out.insert(out.end(), x.second.begin(), x.second.end());
    return out;
}

// id's text, empty when the table has no such entry
std::u16string GetString(Table& t, const std::string& id) {
    auto at = Find(t, id);
    if (at == t.entries.end()) return {};
    const std::vector<uint8_t>& e = at->second;
    size_t p = 1 + e[0] + 1, chars = e[p] | e[p + 1] << 8;
    std::u16string text;
    for (size_t i = 0; i < chars; i++) text += char16_t(e[p + 2 + 2 * i] | e[p + 3 + 2 * i] << 8);
    return text;
}

// every string this file adds, the rows with the settings as they are now
void SetPcStrings(Table& t) {
    // the quit question, as the pause menu's (MessageWarning, MessageRestart) look
    SetString(t, "PCQuitTitle", u"QUIT", "MessageWarning");
    // short enough for one line: the box wraps at its own width, and the title already says QUIT
    SetString(t, "PCQuitBody", u"Are you sure you want to quit?", "MessageRestart");
    // the main menu's help lines, "A SELECT", get "B QUIT"
    for (int i = 2; i <= 8; i++) {
        std::string id = "HelpMainButton" + std::to_string(i);
        std::u16string text = GetString(t, id);
        if (!text.empty() && text.find(u'\x14') == std::u16string::npos)
            SetString(t, id, text + u" \x14" u"QUIT", id.c_str());
    }
    SetString(t, "PCGraphicsButton", u"PC GRAPHICS", "GraphicsOptionsButton1");
    SetString(t, "HelpPCGraphics", u"\n\x13" u"ADJUST PC DISPLAY SETTINGS \x14" u"BACK", "HelpGraphicsOptions");
    SetString(t, "PCGraphicsTitle", u"PC GRAPHICS", "GraphicOptionsMenu_Title");
    SetString(t, "PCGfxBlank", u"", "HelpGraphicsOptions");
    // {_Left}{_Right} are 0x8D 0x8E, {_A} 0x13, {_BBack} 0x14 "BACK"
    SetString(t, "HelpPCGfx", u"\n\x8D\x8E" u"CHANGE \x14" u"BACK", "HelpGraphicsOptions");
    SetString(t, "PCExperimentalButton", u"EXPERIMENTAL", "GraphicsOptionsButton1");
    SetString(t, "HelpPCExperimental", u"\n\x13" u"TRY FEATURES IN TESTING \x14" u"BACK", "HelpGraphicsOptions");
    SetString(t, "PCExpTitle", u"EXPERIMENTAL", "GraphicOptionsMenu_Title");
    SetString(t, "HelpPCExp1", u"\n\x8D\x8E" u"TURN UPSCALED UI ON OR OFF \x14" u"BACK", "HelpGraphicsOptions");
    SetString(t, "HelpPCExp2", u"\n\x13" u"UPSCALE THE UI ON THIS PC \x14" u"BACK", "HelpGraphicsOptions");
    // the build question; each line short enough not to wrap (the box wraps at its own width)
    SetString(t, "PCBuildTitle", u"UPSCALE UI", "MessageWarning");
    SetString(t, "PCBuildBody", u"The game closes to upscale\nits menus, then restarts.", "MessageRestart");
    for (const Screen* screen : kScreens)
        for (uint32_t i = 0; i < screen->count; i++) {
            std::string id = RowId(*screen, i);
            SetString(t, id, RowText(*screen, i), "LiveFeedbackSession", RowEntrySize(id));
        }
}

// ---- the screens ----

uint32_t ListSelection(uint32_t list) { return list ? ::kernel::R32(list + 0x24) : 0; }
// the graphics menu's button list is at +0x2C (its B method reads it there), the feedback and status menus' at +0x28
// (their inits)
uint32_t Selection(uint32_t menu) { return ListSelection(::kernel::R32(menu + 0x2C)); }
bool IsGraphicsMenu(uint32_t menu) { return menu && ::kernel::R32(menu) == kGraphicsMenuVtable; }
bool IsPcMenu(uint32_t menu) { return menu && ::kernel::R32(menu) == kFeedbackVtable; }
bool IsExperimentalMenu(uint32_t menu) { return menu && ::kernel::R32(menu) == kStatusVtable; }
// the screen of settings a menu is, if it is one
const Screen* ScreenOf(uint32_t menu) {
    return IsPcMenu(menu) ? &kPcScreen : IsExperimentalMenu(menu) ? &kExperimentalScreen : nullptr;
}

// the rows' entries rewritten in place with the settings as they are now: the loaded table's count at +0x14, its
// offsets at +0x18 and its strings at +0x1C, each row's entry as long as PatchStringTable made it
bool RefreshStrings() {
    if (!g_table) return false;
    uint32_t count = ::kernel::R32(g_table + 0x14), offs = ::kernel::R32(g_table + 0x18), data = ::kernel::R32(g_table + 0x1C);
    if (!count || !offs || !data) return false;
    uint32_t want = 0, done = 0;
    for (const Screen* screen : kScreens) want += screen->count;
    for (uint32_t i = 0; i < count && done < want; i++) {
        uint32_t at = data + ::kernel::R32(offs + i * 4);
        const uint8_t* e = static_cast<const uint8_t*>(::kernel::HostPtr(at));
        std::string id(reinterpret_cast<const char*>(e + 1), e[0]);
        const Screen* screen = nullptr;
        for (const Screen* s : kScreens) {
            size_t n = std::strlen(s->prefix);
            if (id.size() > n && id.compare(0, n, s->prefix) == 0 && std::isdigit(static_cast<unsigned char>(id[n])))
                screen = s;
        }
        if (!screen) continue;
        uint32_t row = uint32_t(std::atoi(id.c_str() + std::strlen(screen->prefix))) - 1;
        if (row >= screen->count) continue;
        size_t size = RowEntrySize(id);
        if (i + 1 < count && ::kernel::R32(offs + (i + 1) * 4) - ::kernel::R32(offs + i * 4) < size) continue;  // not ours
        uint16_t chars = uint16_t(e[2 + id.size()] | e[3 + id.size()] << 8);
        uint8_t tail[5];
        std::memcpy(tail, e + 4 + id.size() + 2 * chars, 5);
        std::u16string text = RowText(*screen, row);
        if (text.size() > kRowChars) text.resize(kRowChars);
        std::vector<uint8_t> fresh = MakeEntry(id, text, tail, size);
        std::memcpy(::kernel::HostPtr(at), fresh.data(), size);
        done++;
    }
    return done == want;
}

// the list's selected button shows its row's text again: the list built its items once, each keeping its text
void ShowRow(X86Context& ctx, uint32_t list, const Screen& screen, uint32_t row) {
    if (!RefreshStrings() || !list) return;
    // the list's items: an array at +0x4C, their count at +0x50
    uint32_t items = ::kernel::R32(list + 0x4C), count = ::kernel::R32(list + 0x50);
    uint32_t item = items && row < count ? ::kernel::R32(items + row * 4) : 0;
    uint32_t mem = item ? GameAlloc(ctx, 24) : 0;  // the id's text, then the id and the text as CStrings
    uint32_t ecx = ctx.ecx;
    if (mem) {
        std::string id = RowId(screen, row);
        std::memcpy(::kernel::HostPtr(mem), id.c_str(), id.size() + 1);
        uint32_t id_str = mem + 16, text = mem + 20;
        ctx.ecx = id_str;
        ::kernel::CallGuest(ctx, kCStringFromText, {mem});
        ctx.ecx = ::kernel::R32(kGui);
        ::kernel::CallGuest(ctx, kGetText, {text, id_str, 0});
        ctx.ecx = item;
        ::kernel::CallGuest(ctx, kItemSetText, {text});
        ctx.ecx = text;
        ::kernel::CallGuest(ctx, kCStringFree, {});
        ctx.ecx = id_str;
        ::kernel::CallGuest(ctx, kCStringFree, {});
        GameFree(ctx, mem);
    }
    ctx.ecx = list;
    ::kernel::CallGuest(ctx, kListSetSelection, {row, 0});  // the buttons show their items' text again
    ctx.ecx = ecx;
}

// Left (-1), Right (1) or A (0) on a screen of settings: steps the selected row (A steps it forward) or presses it
void ChangeSetting(X86Context& ctx, uint32_t menu, const Screen& screen, int dir) {
    uint32_t list = ::kernel::R32(menu + 0x28), row = ListSelection(list);
    if (row >= screen.count) return;
    const Row& r = screen.rows[row];
    if (r.press) {
        if (dir == 0) {
            LOGR("menu: %s pressed", r.label);
            r.press(ctx);
        }
        return;
    }
    r.step(dir == 0 ? 1 : dir);
    app::Save();
    LOGR("menu: %s", RowString(r).c_str());
    ShowRow(ctx, list, screen, row);
}

// Asks with the game's message box, YES or NO. The box (0x7DCE0, thiscall, ret 0x1C) runs its own loop until it is
// answered and returns 1 for A: (const CString& title, const CString& body, kind, a frame pump or 0 for its own,
// controller mask, CString prompt by value (empty takes the kind's), float timeout or 0). It frees the prompt it was
// given. The title and body are string table ids.
bool AskYesNo(X86Context& ctx, const char* title_id, const char* body_id) {
    uint32_t mem = GameAlloc(ctx, 64);  // the three texts, then the title, body and prompt as CStrings
    if (!mem) return false;
    const char* texts[3] = {title_id, body_id, ""};
    for (int i = 0; i < 3; i++) std::memcpy(::kernel::HostPtr(mem + 16 * i), texts[i], std::strlen(texts[i]) + 1);
    uint32_t title = mem + 48, body = mem + 52, prompt = mem + 56;
    uint32_t ecx = ctx.ecx;
    for (int i = 0; i < 3; i++) {
        ctx.ecx = mem + 48 + 4 * i;
        ::kernel::CallGuest(ctx, kCStringFromText, {mem + 16 * uint32_t(i)});
    }
    ctx.ecx = kMenuManager;
    uint32_t answer = ::kernel::CallGuest(ctx, kMessageBox, {title, body, kMessageYesNo, 0, 0x0F, ::kernel::R32(prompt), 0});
    ctx.ecx = title;
    ::kernel::CallGuest(ctx, kCStringFree, {});
    ctx.ecx = body;
    ::kernel::CallGuest(ctx, kCStringFree, {});
    ctx.ecx = ecx;
    GameFree(ctx, mem);
    LOGR("menu: %s? %s", title_id, answer == 1 ? "yes" : "no");
    return answer == 1;
}

// "Are you sure you want to quit?", and the window closes on YES as Alt+F4 does
void AskToQuit(X86Context& ctx) {
    if (AskYesNo(ctx, "PCQuitTitle", "PCQuitBody")) PostMessageW(static_cast<HWND>(app::Window()), WM_CLOSE, 0, 0);
}

// pushes a menu by its name in the image, as the Options menu opens its pages
void OpenMenu(X86Context& ctx, uint32_t menu_name, const char* label) {
    RefreshStrings();
    uint32_t name = GameAlloc(ctx, 4);
    if (!name) return;
    uint32_t ecx = ctx.ecx;
    ctx.ecx = name;
    ::kernel::CallGuest(ctx, kCStringFromText, {menu_name});
    ctx.ecx = kMenuManager;
    uint32_t pushed = ::kernel::CallGuest(ctx, kPushMenu, {name});
    ctx.ecx = name;
    ::kernel::CallGuest(ctx, kCStringFree, {});
    ctx.ecx = ecx;
    GameFree(ctx, name);
    LOGR("menu: %s opened (%08X)", label, pushed);
}

// A control of the menu being set up, by its name in the image: scene + 4's object finds it (0x482F0, thiscall
// (out, const CString& name, 1)) into a reference counted pointer, released at once as the game's inits do (the scene
// keeps its own). 0 when it has none.
uint32_t FindControl(X86Context& ctx, uint32_t scene, uint32_t control_name) {
    uint32_t mem = GameAlloc(ctx, 8);  // the name as a CString, the found control
    if (!mem) return 0;
    uint32_t ecx = ctx.ecx;
    ::kernel::W32(mem + 4, 0);
    ctx.ecx = mem;
    ::kernel::CallGuest(ctx, kCStringFromText, {control_name});
    ctx.ecx = ::kernel::R32(scene + 4);
    ::kernel::CallGuest(ctx, kFindControl, {mem + 4, mem, 1});
    uint32_t control = ::kernel::R32(mem + 4);
    if (control) {
        uint32_t refs = ::kernel::R32(control + 4) - 1;
        ::kernel::W32(control + 4, refs);
        if (refs == 0) {
            ctx.ecx = control;
            ::kernel::CallGuest(ctx, ::kernel::R32(::kernel::R32(control) + 0x0C), {});
            control = 0;
        }
    }
    ctx.ecx = mem;
    ::kernel::CallGuest(ctx, kCStringFree, {});
    ctx.ecx = ecx;
    GameFree(ctx, mem);
    return control;
}

// shows or hides a control's node (its +0x20, whose method +0x30 does it)
void ShowControl(X86Context& ctx, uint32_t control, bool show) {
    uint32_t node = control ? ::kernel::R32(control + 0x20) : 0;
    if (!node) return;
    uint32_t ecx = ctx.ecx;
    ctx.ecx = node;
    ::kernel::CallGuest(ctx, ::kernel::R32(::kernel::R32(node) + 0x30), {show ? 1u : 0u});
    ctx.ecx = ecx;
}

// the list a control is (0x4C280 on its node), 0 when it has none
uint32_t ControlList(X86Context& ctx, uint32_t control) {
    uint32_t node = control ? ::kernel::R32(control + 0x20) : 0;
    return node ? ::kernel::CallGuest(ctx, kControlList, {node}) : 0;
}

}  // namespace

namespace links {
// the FPS counter, in the top left corner (GameInfo::ui)
void DrawPcOverlay(double fps) {
    if (!g_fps_counter) return;
    ImGui::SetNextWindowPos(ImVec2(8, 8));
    ImGui::SetNextWindowBgAlpha(0.45f);
    ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##fps", nullptr, f)) ImGui::Text("%.0f FPS", fps);
    ImGui::End();
}
}  // namespace links

// 0x43A01, Rui_ReadLuaFile before it loads and runs the file: [esp] the bytes, [esp + 4] their size, [esp + 8] the
// file's name. edi holds the bytes too, which the loader frees after the call.
void PatchLuaFile(X86Context& ctx, uint8_t*) {
    std::string name = ::kernel::ReadCString(::kernel::R32(ctx.esp + 8), 260);
    if (EndsWithNoCase(name, "XGraphicsOptionsMenu.scene")) {
        if (ReplaceLuaFile(ctx, kGraphicsScene, uint32_t(sizeof(kGraphicsScene) - 1)))
            LOGR("menu: %s with PC GRAPHICS and EXPERIMENTAL", name.c_str());
        else
            LOGE("menu: no memory for the graphics menu scene, PC GRAPHICS is left out");
        return;
    }
    if (EndsWithNoCase(name, "XMainMenu.scene")) {
        if (ReplaceLuaFile(ctx, kMainScene, uint32_t(sizeof(kMainScene) - 1)))
            LOGR("menu: %s with B to quit", name.c_str());
        return;
    }
    if (EndsWithNoCase(name, "XLiveFeedbackMenu.scene")) {
        if (ReplaceLuaFile(ctx, kPcScene, uint32_t(sizeof(kPcScene) - 1)))
            LOGR("menu: %s is the PC GRAPHICS screen", name.c_str());
        return;
    }
    if (EndsWithNoCase(name, "XLiveStatusMenu.scene")) {
        if (ReplaceLuaFile(ctx, kExperimentalScene, uint32_t(sizeof(kExperimentalScene) - 1)))
            LOGR("menu: %s is the EXPERIMENTAL screen", name.c_str());
        return;
    }
    bool graphics = EndsWithNoCase(name, "XGraphicsOptionsMenu.sceneData");
    bool feedback = EndsWithNoCase(name, "XLiveFeedbackMenu.sceneData");
    bool status = EndsWithNoCase(name, "XLiveStatusMenu.sceneData");
    if (!graphics && !feedback && !status) return;
    uint32_t buf = ::kernel::R32(ctx.esp), size = ::kernel::R32(ctx.esp + 4);
    const uint8_t* p = static_cast<const uint8_t*>(::kernel::HostPtr(buf));
    std::vector<uint8_t> code(p, p + size);
    if (size < 22 || std::memcmp(code.data(), "\x1BLuaP", 5) != 0) return;
    if (graphics) {
        uint16_t ids[2] = {};
        for (int slot = 2; slot <= 3; slot++)
            if (!AddSlotNodes(code, slot, ids[slot - 2]) || !WireSlotControl(code, slot, ids[slot - 2])) {
                LOGE("menu: %s is not laid out as expected, button %d is not wired", name.c_str(), slot);
                return;
            }
        if (ReplaceLuaFile(ctx, code.data(), uint32_t(code.size())))
            LOGR("menu: %s, second and third buttons wired as controls %u, %u and %u, %u", name.c_str(), ids[0],
                 ids[0] + 1, ids[1], ids[1] + 1);
        return;
    }
    auto retext = [&](const std::pair<const char*, const char*>* list, size_t count) {
        int changed = 0;
        for (size_t i = 0; i < count; i++) {
            std::string f = list[i].first, t = list[i].second;
            changed += EditStringConstant(code, [&](const std::string& s, std::string& out) {
                if (s != f) return false;
                out = t;
                return true;
            });
        }
        return changed;
    };
    if (status) {
        int changed = retext(kExperimentalLayoutText, std::size(kExperimentalLayoutText));
        if (changed && ReplaceLuaFile(ctx, code.data(), uint32_t(code.size())))
            LOGR("menu: %s, the title is EXPERIMENTAL's", name.c_str());
        return;
    }
    int changed = retext(kPcLayoutText, std::size(kPcLayoutText));
    int moved = SpaceSlots(code);
    uint16_t arrows = 0;
    bool scroll = AddArrowNodes(code, arrows) && WireArrows(code, arrows);
    if (!scroll) LOGE("menu: %s is not laid out as expected, no scroll arrows", name.c_str());
    if ((changed || moved || scroll) && ReplaceLuaFile(ctx, code.data(), uint32_t(code.size())))
        LOGR("menu: %s, %d of %d texts are PC GRAPHICS's, %d of 5 buttons spaced evenly, scroll arrows %s",
             name.c_str(), changed, int(std::size(kPcLayoutText)), moved, scroll ? "wired" : "left out");
}

// 0x43A06, Rui_ReadLuaFile just after the file ran: esi is the name (a CString, its text at [esi]), ebx the Lua state
// object the run went through
void LuaFileRan(X86Context& ctx, uint8_t*) {
    uint32_t name = ::kernel::R32(ctx.esi);
    std::string n = ::kernel::ReadCString(name, 260);
    std::string lua;
    const char* what;
    if (EndsWithNoCase(n, "XGraphicsOptionsMenu.sceneData")) {
        lua = GraphicsLayoutPatch();
        what = "second and third button slots filled";
    } else if (EndsWithNoCase(n, "XLiveFeedbackMenu.sceneData")) {
        lua = PcLayoutPatch();
        what = "scroll arrows and bars placed";
    } else if (EndsWithNoCase(n, "XLiveStatusMenu.sceneData")) {
        lua = kBackgroundPatch;
        what = "bars placed";
    } else {
        return;
    }
    uint32_t size = uint32_t(lua.size());
    uint32_t text = GameAlloc(ctx, size);
    if (!text) return;
    std::memcpy(::kernel::HostPtr(text), lua.data(), size);
    uint32_t ecx = ctx.ecx;
    ctx.ecx = ctx.ebx;
    uint32_t result = ::kernel::CallGuest(ctx, kRunLua, {text, size, name});
    ctx.ecx = ecx;
    GameFree(ctx, text);
    LOGR("menu: %s, %s (run returned %u)", n.c_str(), what, result);
}

// 0x57FA0, the string table load after its file was read into [esi + 0x0C]: the strings this file adds go in, before
// the load points the table at its parts
void PatchStringTable(X86Context& ctx, uint8_t*) {
    uint32_t table = ctx.esi, buf = ::kernel::R32(table + 0x0C);
    Table t;
    if (!buf || !ReadTable(buf, t)) return;
    if (Find(t, "GraphicsOptionsButton1") == t.entries.end()) return;  // not the menus' table
    SetPcStrings(t);
    std::vector<uint8_t> out = WriteTable(t);
    uint32_t fresh = GameAlloc(ctx, uint32_t(out.size()));
    if (!fresh) {
        LOGE("menu: no memory for the string table, PC GRAPHICS has no text");
        return;
    }
    std::memcpy(::kernel::HostPtr(fresh), out.data(), out.size());
    GameFree(ctx, buf);
    ::kernel::W32(table + 0x0C, fresh);
    g_table = table;
    LOGR("menu: string table %u entries, PC GRAPHICS text added", uint32_t(t.entries.size()));
}

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
// detail out to far x k + bias, and fades out past that (its LOD entry's +0x34, +0x38 and +0xA0). k is 0x434CA0, which
// the camera's update (0x1254D0, every frame) works out from its field of view and the hole's RangeMultiplier
// (0x434CA4, from the course's settings, CourseSettings Hole<n>, 1 to 5). 0x125983, just after it stored k: k times
// LOD DISTANCE.
// The fades step 1/30 a frame times 0x434CA8 (1, read nowhere else): it becomes the frame's time in 1/30 s, so they
// take as long as on the Xbox at any frame rate.
void ScaleLodDistance(X86Context&, uint8_t*) {
    constexpr uint32_t kLodScale = 0x00434CA0, kLodFadeRate = 0x00434CA8;
    float k = float(std::clamp(links::g_lod_percent, 25u, 1000u)) / 100.0f, v;
    uint32_t raw = ::kernel::R32(kLodScale);
    std::memcpy(&v, &raw, 4);
    v *= k;
    std::memcpy(&raw, &v, 4);
    ::kernel::W32(kLodScale, raw);
    float dt = FrameTime();
    float rate = links::g_original_fps || !(dt > 0.0f) ? 1.0f : std::min(dt * 30.0f, 3.0f);
    std::memcpy(&raw, &rate, 4);
    ::kernel::W32(kLodFadeRate, raw);
    static bool logged = false;
    if (!logged) {
        logged = true;
        LOGR("lod: detail distance multiplier %.3f, LOD DISTANCE %u%% makes it %.3f", v / k, links::g_lod_percent, v);
    }
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

// The in-round background's geometry (see the top of the file). A mesh in a .geom file is its name, its sub-mesh count,
// then each sub-mesh: its vertex format and vertex bytes, the vertices, its index bytes, the 16 bit indices, its shader
// type and name and a flag byte. The untextured meshes here are format 0x152, 36 bytes a vertex: position, normal,
// a D3DCOLOR, texture u and v.
namespace {

// the Graphics Options menu's widescreen bars in the menu's frame (its Color_BackgroundShape at y -15 plus each bar's
// own place and vertices): bottom and top y; the full width of the 640 x 480 layout
struct Rect {
    float bottom, top;
};
constexpr Rect kTint{-155.5f, 161.5f};  // a faint tint over the middle
constexpr Rect kTopBar{160.0f, 240.0f};  // behind the title
constexpr Rect kBottomBar{-241.0f, -152.0f};  // behind the help line

// a mesh made rectangles, one quad each, of one colour
struct Bar {
    const char* mesh;
    Rect rects[2];
    uint32_t count;
    uint8_t alpha;
};
// The tint must be TitleBackgroundColor: a mesh's flag byte decides whether it is drawn blended, and only that one's
// is set (the BackgroundColorShape meshes draw opaque, which made a 20% tint a solid screen). It is drawn first, so the
// bars, drawn after it, cover its edges.
const Bar kPcBars[] = {
    {"TitleBackgroundColor", {kTint}, 1, 0x33},
    {"BackgroundColorShape2", {kTopBar}, 1, 0xFF},
    {"BackgroundColorShape1", {kBottomBar}, 1, 0xFF},
};
// XLiveStatusMenu's background has one opaque mesh, with room for both bars (45 vertices, 63 indices)
const Bar kExperimentalBars[] = {
    {"TitleBackgroundColor", {kTint}, 1, 0x33},
    {"BackgroundColorShape1", {kTopBar, kBottomBar}, 2, 0xFF},
};
constexpr uint8_t kBarBgr[3] = {0x15, 0x12, 0x0F};  // the bars' dark blue, as a D3DCOLOR's first three bytes
constexpr uint32_t kColourFvf = 0x152, kColourStride = 36;

// the screen whose geometry is loading, null for any other layout
struct Backdrop {
    const char* layout;  // in the geometry's name
    const char* label;
    const Bar* bars;
    size_t count;
};
const Backdrop kBackdrops[] = {
    {"XLiveFeedbackMenu", "PC GRAPHICS", kPcBars, std::size(kPcBars)},
    {"XLiveStatusMenu", "EXPERIMENTAL", kExperimentalBars, std::size(kExperimentalBars)},
};
const Backdrop* g_backdrop = nullptr;
size_t g_bars_made = 0;  // its meshes made bars so far
}  // namespace

// 0x265C50, a layout's geometry about to load: [esp + 4] its name, a const CString& (the address of the CString, whose
// text is at the address it holds; it copies it with 0x336E4), "XLiveFeedbackMenu" or "XLiveStatusMenu" in it for
// these screens
void PcGeometryLoad(X86Context& ctx, uint8_t*) {
    uint32_t name_ref = ::kernel::R32(ctx.esp + 4);
    std::string name = name_ref ? ::kernel::ReadCString(::kernel::R32(name_ref), 260) : std::string();
    g_backdrop = nullptr;
    for (const Backdrop& b : kBackdrops)
        if (name.find(b.layout) != std::string::npos) g_backdrop = &b;
    if (g_backdrop) {
        g_bars_made = 0;
        LOGR("menu: %s's geometry loading (%s)", g_backdrop->label, name.c_str());
    }
}

// 0x2657F5 in the mesh reader (0x2656F0), its first sub-mesh's vertices and indices just read: esi the sub-mesh (its
// vertex format +0, stride +4, vertex count +8, index count +0x0C, vertex buffer +0x10 whose data is at +0x14 as a
// physical address, index buffer +0x1C whose data is at +0x20), [esp + 0x1C] the mesh + 4 (its name, a CString, at +0)
void PcGeometryMesh(X86Context& ctx, uint8_t*) {
    if (!g_backdrop) return;
    uint32_t mesh = ::kernel::R32(ctx.esp + 0x1C) - 4;
    std::string name = ::kernel::ReadCString(::kernel::R32(mesh), 64);
    const Bar* bar = nullptr;
    for (size_t i = 0; i < g_backdrop->count; i++)
        if (name == g_backdrop->bars[i].mesh) bar = &g_backdrop->bars[i];
    if (!bar) return;
    uint32_t sub = ctx.esi;
    uint32_t fvf = ::kernel::R32(sub), stride = ::kernel::R32(sub + 4), verts = ::kernel::R32(sub + 8);
    uint32_t indices = ::kernel::R32(sub + 0x0C);
    uint32_t vb = ::kernel::R32(sub + 0x14) | 0x80000000u, ib = ::kernel::R32(sub + 0x20);
    if (fvf != kColourFvf || stride != kColourStride || verts < 4 * bar->count || indices < 6 * bar->count || !ib) {
        LOGE("menu: %s mesh %s is not as expected (format %X, %u vertices, %u indices)", g_backdrop->label,
             name.c_str(), fvf, verts, indices);
        return;
    }
    // each rectangle's corners 0 to 3 counter-clockwise from the bottom left; every other vertex on the first corner,
    // every other index 0
    for (uint32_t i = 0; i < verts; i++) {
        uint32_t r = i / 4 < bar->count ? i / 4 : 0, c = i / 4 < bar->count ? i % 4 : 0;
        const Rect& rect = bar->rects[r];
        float v[8] = {c == 0 || c == 3 ? -320.0f : 320.0f, c < 2 ? rect.bottom : rect.top, 0, 0, 0, 1, 0, 0};
        uint8_t* p = static_cast<uint8_t*>(::kernel::HostPtr(vb + i * kColourStride));
        std::memcpy(p, v, 24);
        p[24] = kBarBgr[0];
        p[25] = kBarBgr[1];
        p[26] = kBarBgr[2];
        p[27] = bar->alpha;
        std::memcpy(p + 28, v + 6, 8);
    }
    const uint16_t quad[6] = {2, 3, 0, 0, 1, 2};
    for (uint32_t i = 0; i < indices; i++) {
        uint16_t x = i / 6 < bar->count ? uint16_t(quad[i % 6] + 4 * (i / 6)) : 0;
        std::memcpy(::kernel::HostPtr(ib + i * 2), &x, 2);
    }
    LOGR("menu: %s mesh %s is now %u widescreen bar(s) (%u vertices, %u indices)", g_backdrop->label, name.c_str(),
         bar->count, verts, indices);
    if (++g_bars_made == g_backdrop->count)
        LOGR("menu: %s's in-round background is the widescreen bars (%u meshes)", g_backdrop->label,
             unsigned(g_bars_made));
}

// 0xA84B7 in XLiveFeedbackMenu's init: about to show ([esp] 1) or hide (0) the LIVE and XSN logos. Always hidden.
void HidePcLogos(X86Context& ctx, uint8_t*) { ::kernel::W32(ctx.esp, 0); }

// XGraphicsOptionsMenu Left (0x93B10) and Right (0x93B60): brightness down and up, ret 4. Only on BRIGHTNESS.
X86_EXTERN_FUNC(__imp__sub_00093B10);
X86_FUNC(sub_00093B10) {
    if (Selection(ctx.ecx) != 0) return ::kernel::Return(ctx, 0, 1);
    __imp__sub_00093B10(ctx, base);
}
X86_EXTERN_FUNC(__imp__sub_00093B60);
X86_FUNC(sub_00093B60) {
    if (Selection(ctx.ecx) != 0) return ::kernel::Return(ctx, 0, 1);
    __imp__sub_00093B60(ctx, base);
}

// XLiveFeedbackMenu's A method (0xA85C0, ret 4) sent Xbox Live feedback: on the PC GRAPHICS screen A steps the
// selected setting forward
X86_FUNC(sub_000A85C0) {
    uint32_t menu = ctx.ecx;
    ::kernel::Return(ctx, 0, 1);
    if (IsPcMenu(menu)) ChangeSetting(ctx, menu, kPcScreen, 0);
}

// XLiveStatusMenu's init (0xAB730, thiscall (scene, arg), ret 8, returns 0), which asked Xbox Live for the player's
// state, as the EXPERIMENTAL screen's: what its own init does apart from Live
X86_FUNC(sub_000AB730) {
    uint32_t menu = ctx.ecx, scene = ::kernel::R32(ctx.esp + 4), arg = ::kernel::R32(ctx.esp + 8);
    uint32_t ebx = ctx.ebx, esi = ctx.esi, edi = ctx.edi, ebp = ctx.ebp;
    // the menu manager learns of the menu, as every init starts
    uint32_t entered = ::kernel::CallGuest(ctx, kMenuEntered, {});
    ctx.ecx = kMenuManager;
    ::kernel::CallGuest(ctx, kManagerEntered, {entered});
    // the in-round background in a round; never the LIVE logos
    bool in_round = *static_cast<const uint8_t*>(::kernel::HostPtr(kInRound)) != 0;
    ShowControl(ctx, FindControl(ctx, scene, kIngameBackgroundName), in_round);
    ShowControl(ctx, FindControl(ctx, scene, kLiveLogosName), false);
    ctx.ecx = menu;
    ::kernel::CallGuest(ctx, kMenuBaseInit, {scene, arg});
    // the player's controller (its B method answers only that one's), from the GUI object's +0x11CC
    uint32_t gui = ::kernel::R32(kGui);
    ::kernel::W32(menu + 0x30, uint32_t(int32_t(int8_t(*static_cast<const uint8_t*>(::kernel::HostPtr(gui + 0x11CC))))));
    // the status grids hidden, as the original hides them offline (their method +0x5C)
    for (uint32_t grid_name : {kOnlineName, kVoiceMaskName}) {
        uint32_t grid = ControlList(ctx, FindControl(ctx, scene, grid_name));
        if (!grid) continue;
        ctx.ecx = grid;
        ::kernel::CallGuest(ctx, ::kernel::R32(::kernel::R32(grid) + 0x5C), {0});
    }
    // the button list at +0x28, its selection where the screen was left (+0x2C, which its B method keeps)
    uint32_t list = ControlList(ctx, FindControl(ctx, scene, kButtonListName));
    ::kernel::W32(menu + 0x28, list);
    uint32_t selection = ::kernel::R32(menu + 0x2C);
    if (selection >= kExperimentalRowCount) selection = 0;
    RefreshStrings();
    if (list) {
        ctx.ecx = list;
        ::kernel::CallGuest(ctx, kListSetSelection, {selection, 0});
    } else {
        LOGE("menu: the EXPERIMENTAL screen has no button list");
    }
    LOGR("menu: EXPERIMENTAL set up (%s)", in_round ? "in a round" : "menus");
    ctx.ebx = ebx;
    ctx.esi = esi;
    ctx.edi = edi;
    ctx.ebp = ebp;
    ctx.ecx = menu;
    ::kernel::Return(ctx, 0, 2);
}

// XLiveStatusMenu's Left (0xAB590) and Right (0xAB620), ret 4, changed the Live state of the selected grid: nothing
// (the glue changes the selected setting)
X86_FUNC(sub_000AB590) { ::kernel::Return(ctx, 0, 1); }
X86_FUNC(sub_000AB620) { ::kernel::Return(ctx, 0, 1); }

// The scenes' "OnHitAButton()", "OnHitLeft()" and "OnHitRight()" (0x79170, 0x79330, 0x793A0, ret 4): the current
// menu (at +4) gets its A, Left or Right method, after the menu sound. A on PC GRAPHICS or EXPERIMENTAL opens that
// screen. On the screens, Left and Right change the selected setting (the menus' own Left and Right do nothing), and on
// EXPERIMENTAL so does A (its A method is the shared empty one; PC GRAPHICS's is sub_000A85C0).
X86_EXTERN_FUNC(__imp__sub_00079170);
X86_FUNC(sub_00079170) {
    uint32_t menu = ::kernel::R32(ctx.ecx + 4);
    bool graphics = IsGraphicsMenu(menu);
    uint32_t item = graphics ? Selection(menu) : 0;
    bool experimental = IsExperimentalMenu(menu);
    __imp__sub_00079170(ctx, base);
    if (graphics && item == kPcGraphicsItem) OpenMenu(ctx, kFeedbackName, "PC GRAPHICS");
    if (graphics && item == kExperimentalItem) OpenMenu(ctx, kStatusName, "EXPERIMENTAL");
    if (experimental) ChangeSetting(ctx, menu, kExperimentalScreen, 0);
}
// "OnHitBButton()" (0x791E0, ret 4): the menu's B method after the back sound; on the main menu, the quit question
X86_EXTERN_FUNC(__imp__sub_000791E0);
X86_FUNC(sub_000791E0) {
    uint32_t menu = ::kernel::R32(ctx.ecx + 4);
    bool main = menu && ::kernel::R32(menu) == kMainMenuVtable;
    __imp__sub_000791E0(ctx, base);
    if (main) AskToQuit(ctx);
}
X86_EXTERN_FUNC(__imp__sub_00079330);
X86_FUNC(sub_00079330) {
    uint32_t menu = ::kernel::R32(ctx.ecx + 4);
    const Screen* screen = ScreenOf(menu);
    __imp__sub_00079330(ctx, base);
    if (screen) ChangeSetting(ctx, menu, *screen, -1);
}
X86_EXTERN_FUNC(__imp__sub_000793A0);
X86_FUNC(sub_000793A0) {
    uint32_t menu = ::kernel::R32(ctx.ecx + 4);
    const Screen* screen = ScreenOf(menu);
    __imp__sub_000793A0(ctx, base);
    if (screen) ChangeSetting(ctx, menu, *screen, 1);
}
