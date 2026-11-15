// menu_mouse.h: what is on screen, the lists and grids, the matching and the presses.
//
// What is on screen (MenuDraw). The menus draw each piece of text as one quad list, four vertices a character (spaces
// too, a line break not), through a vertex program that multiplies the position by c0 to c3 and scales it to the
// screen with c-38 and c-37. A button's text is drawn twice, its shadow first; the two are taken as one. Every other
// small draw (a picture, an arrow, a slider: at most kShapeVertices vertices) is kept as a shape, with the vertices
// it was drawn from: the cells of a grid are the same mesh drawn once each.
//
// A list (the constructor 0x5CC00's class and those derived from it): its first shown item at +0x20, the selected one
// at +0x24, how many buttons it shows at +0x40, its items at +0x4C and their count at +0x50. An item's text is a
// CStringW at +0x0C: a pointer to UTF-16 characters whose count is 12 bytes before them. A destroyed list's memory
// may be read after it is gone, so every read is checked and nothing read is trusted further than that.
//
// Matching a list: its shown items' character counts, in order, are looked for among the texts going down the screen
// (then across it, for a list laid out sideways), each below the one before and in line with it. Most of the items
// must be found, evenly spaced; one that is not (its text is drawn another way) is placed by the spacing of the rest.
//
// A row's value: what is drawn on an item's row to the right of its text (the value's text, its arrows, a slider) is
// the value's place. A click on its left half is Left, on its right half Right; the wheel over the row is Left and
// Right too. An item whose own text has the font's arrows around it (0x7F, 0x80) takes Left and Right on those.
//
// A grid (RGuiGridNode, constructor 0x63940): the selected cell at +0x5C, its cells' holder at +0x1C (the usual one,
// vtable 0x3DDBB8: the cells at +0x0C, their count at +0x10, each cell's row at +0x1C, the rows at +0x30). Its cells
// are taken to be the one mesh that is drawn as many times as the grid has cells, in rows down the screen.
#include "common/menu_mouse.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace links {

namespace {

using Clock = std::chrono::steady_clock;
int64_t NowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(); }

// ---- what is on screen ----

struct Box {
    float x0, y0, x1, y1;  // in the game's frame, 720 x 480 on the console
    uint32_t chars;        // a text's vertices
};
struct Shape {
    Box box;
    const void* mesh;  // the vertices it was drawn from
    uint32_t count;
};
constexpr uint32_t kQuadList = 8;       // D3DPT_QUADLIST
constexpr uint32_t kShapeVertices = 64;
constexpr int kScreenScale = 96 - 38;   // c-38 in hardware order, then c-37 its offset
constexpr int kTransform = 96;          // c0 to c3

struct Screen {
    std::vector<Box> texts;
    std::vector<Shape> shapes;
};
std::mutex g_screen_lock;
Screen g_drawing, g_shown;  // the frame being drawn, and the last finished one
uint32_t g_frame = 0;
int64_t g_shown_at = 0;
uint64_t g_shown_sign = 0;  // what g_shown holds, in short: a new value is a changed screen
float g_frame_w = 720, g_frame_h = 480;

float Width(const Box& b) { return b.x1 - b.x0; }
float Height(const Box& b) { return b.y1 - b.y0; }
bool Inside(const Box& b, float x, float y) { return b.x1 > b.x0 && x >= b.x0 && x < b.x1 && y >= b.y0 && y < b.y1; }

// a screen in short (FNV-1a over its texts' and shapes' places and sizes, to the pixel): the same for the same screen
uint64_t Sign(const Screen& screen) {
    uint64_t h = 1469598103934665603ull;
    auto add = [&](int64_t v) {
        for (int i = 0; i < 8; i++, v >>= 8) h = (h ^ uint64_t(v & 0xFF)) * 1099511628211ull;
    };
    auto box = [&](const Box& b) {
        add(std::lround(b.x0));
        add(std::lround(b.y0));
        add(std::lround(b.x1));
        add(std::lround(b.y1));
        add(b.chars);
    };
    for (const Box& b : screen.texts) box(b);
    add(-1);
    for (const Shape& s : screen.shapes) box(s.box);
    return h;
}

// ---- the lists and grids ----

constexpr uint32_t kImageData = 0x003D0000, kImageEnd = 0x00450000;  // where the game's vtables are
constexpr uint32_t kCellHolderVtable = 0x003DDBB8;
constexpr size_t kKnown = 1024;  // the game makes every menu's lists as it starts
std::mutex g_known_lock;
std::vector<uint32_t> g_lists, g_grids;  // the newest first
std::atomic<uint32_t> g_known_changes{0};  // a list or grid made, selected or gone

// Every page of a guest range is there to read: the guest's own record of what it has committed (mem::IsCommitted, a
// table lookup), else what Windows says of the host's page (memory the record does not cover). Not IsBadReadPtr,
// which reads each page under an exception handler: slow, and it can set off a guard page.
bool Readable(uint32_t address, size_t size) {
    uint64_t end = uint64_t(address) + size;
    for (uint64_t page = address & ~uint64_t(mem::kPage - 1); page < end; page += mem::kPage) {
        if (mem::IsCommitted(uint32_t(page))) continue;
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(::kernel::HostPtr(uint32_t(page)), &info, sizeof info)) return false;
        if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    }
    return true;
}

// a read that may be of memory the game has let go
bool Read(uint32_t address, void* out, size_t size) {
    if (address < 0x10000 || address > 0xFFFFFFFFu - size) return false;
    if (!Readable(address, size)) return false;
    std::memcpy(out, ::kernel::HostPtr(address), size);
    return true;
}
bool Read32(uint32_t address, uint32_t& v) { return Read(address, &v, 4); }

// a character the font draws: the printable ones and the controller's buttons (0x13 to 0x1C)
bool Drawn(uint16_t c) { return c >= 0x20 || (c >= 0x13 && c <= 0x1C); }

struct List {
    uint32_t address = 0;
    int first = 0, selected = 0, count = 0;
    std::vector<uint32_t> lengths;  // the shown items' drawn characters
    std::vector<uint8_t> arrows;    // bit 0: the text starts with the left arrow, bit 1: ends with the right one
};

bool ReadList(uint32_t address, List& out) {
    uint32_t vtable, first, selected, buttons, items, count;
    if (!Read32(address, vtable) || vtable < kImageData || vtable >= kImageEnd) return false;
    if (!Read32(address + 0x20, first) || !Read32(address + 0x24, selected) || !Read32(address + 0x40, buttons) ||
        !Read32(address + 0x4C, items) || !Read32(address + 0x50, count))
        return false;
    if (!count || count > 512 || !buttons || buttons > 64 || first >= count || selected >= count || !items) return false;
    out.address = address;
    out.first = int(first);
    out.selected = int(selected);
    out.count = int(count);
    out.lengths.clear();
    out.arrows.clear();
    uint32_t end = std::min(count, first + buttons);
    for (uint32_t i = first; i < end; i++) {
        uint32_t item, text, length;
        if (!Read32(items + i * 4, item) || !Read32(item + 0x0C, text) || !Read32(text - 0x0C, length)) return false;
        if (length > 256) return false;
        uint16_t chars[256];
        if (length && !Read(text, chars, size_t(length) * 2)) return false;
        uint32_t drawn = 0;
        uint8_t arrows = 0;
        for (uint32_t c = 0; c < length; c++) {
            // a colour change, 0x03 then ":aarrggbb", draws nothing
            if (chars[c] == 0x03 && c + 9 < length && chars[c + 1] == ':') c += 9;
            else drawn += Drawn(chars[c]);
            if (chars[c] == 0x7F) arrows |= 1;
            if (chars[c] == 0x80) arrows |= 2;
        }
        out.lengths.push_back(drawn);
        out.arrows.push_back(arrows);
    }
    return true;
}

struct Grid {
    uint32_t address = 0;
    int selected = 0, count = 0;
    std::vector<int> row;  // each cell's
};

bool ReadGrid(uint32_t address, Grid& out) {
    uint32_t vtable, holder, holder_vtable, selected, cells, count, rows_at;
    if (!Read32(address, vtable) || vtable < kImageData || vtable >= kImageEnd) return false;
    if (!Read32(address + 0x5C, selected) || !Read32(address + 0x1C, holder) || !Read32(holder, holder_vtable)) return false;
    if (holder_vtable != kCellHolderVtable) return false;
    if (!Read32(holder + 0x0C, cells) || !Read32(holder + 0x10, count) || !Read32(holder + 0x1C, rows_at)) return false;
    if (count < 2 || count > 256 || selected >= count || !cells) return false;
    out.address = address;
    out.selected = int(selected);
    out.count = int(count);
    out.row.assign(count, 0);
    for (uint32_t i = 0; i < count && rows_at; i++) {
        uint32_t r = 0;
        if (!Read32(rows_at + i * 4, r) || r > 255) return false;
        out.row[i] = int(r);
    }
    return true;
}

// ---- matching ----

// something the mouse can be over
struct Spot {
    Box rect{};
    Box text{};   // a list item's own text
    Box value{};  // what is on its row to the right, empty (x1 <= x0) without
    uint8_t arrows = 0;
    bool grid = false;
    uint32_t object = 0;  // the list or grid
    int index = -1;       // the item or cell
    bool sideways = false;
    float found = 0;      // how much of the list was found, to choose between two over the same place
};

float Along(const Box& b, bool sideways) { return sideways ? (b.x0 + b.x1) / 2 : (b.y0 + b.y1) / 2; }
float Across(const Box& b, bool sideways) { return sideways ? (b.y0 + b.y1) / 2 : b.x0; }

struct Run {
    std::vector<int> box;  // each shown item's text, -1 when it was not found
    int found = 0;
    float start = 0, step = 0, error = 1e9f;  // an item's place along the list: start + step x its number
};

// the list's items among the texts, going down (or across) the screen; false when too few are there
bool Chain(const List& list, const std::vector<Box>& boxes, bool sideways, Run& best) {
    int n = int(list.lengths.size()), with_text = 0;
    for (uint32_t len : list.lengths) with_text += len != 0;
    if (!with_text) return false;
    int need = with_text == 1 ? 1 : std::max(2, (with_text * 3 + 4) / 5);  // most of them
    best = Run{};
    for (int lead = 0; lead < n && lead <= n - need; lead++) {
        if (!list.lengths[size_t(lead)]) continue;
        int starts = 0;
        for (size_t s = 0; s < boxes.size(); s++) {
            if (boxes[s].chars != list.lengths[size_t(lead)] * 4) continue;
            starts++;
            Run run;
            run.box.assign(size_t(n), -1);
            run.box[size_t(lead)] = int(s);
            run.found = 1;
            int prev = int(s);
            for (int i = lead + 1; i < n; i++) {
                if (!list.lengths[size_t(i)]) continue;
                int next = -1;
                for (size_t b = 0; b < boxes.size(); b++) {
                    const Box& c = boxes[b];
                    const Box& p = boxes[size_t(prev)];
                    if (c.chars != list.lengths[size_t(i)] * 4) continue;
                    if (Along(c, sideways) <= Along(p, sideways) + 2) continue;
                    if (std::fabs(Across(c, sideways) - Across(p, sideways)) > 70) continue;  // not in line
                    if (next < 0 || Along(c, sideways) < Along(boxes[size_t(next)], sideways)) next = int(b);
                }
                if (next < 0) continue;
                run.box[size_t(i)] = next;
                run.found++;
                prev = next;
            }
            if (run.found < need) continue;
            if (run.found == 1) {
                run.start = Along(boxes[s], sideways) - float(lead) * 1.0f;
                run.step = 0;
                run.error = 0;
            } else {
                // the line through the found ones' places, by their numbers
                float sn = 0, sp = 0, snn = 0, snp = 0;
                for (int i = 0; i < n; i++) {
                    if (run.box[size_t(i)] < 0) continue;
                    float p = Along(boxes[size_t(run.box[size_t(i)])], sideways);
                    sn += float(i);
                    sp += p;
                    snn += float(i) * float(i);
                    snp += float(i) * p;
                }
                float m = float(run.found), d = m * snn - sn * sn;
                if (std::fabs(d) < 1e-3f) continue;
                run.step = (m * snp - sn * sp) / d;
                run.start = (sp - run.step * sn) / m;
                // items do not overlap: a step is at least a line of text
                float tall = 0;
                for (int i = 0; i < n; i++)
                    if (run.box[size_t(i)] >= 0) tall = std::max(tall, sideways ? Width(boxes[size_t(run.box[size_t(i)])]) : Height(boxes[size_t(run.box[size_t(i)])]));
                if (run.step < 6 || run.step < tall * 0.9f) continue;
                run.error = 0;
                for (int i = 0; i < n; i++) {
                    if (run.box[size_t(i)] < 0) continue;
                    float p = Along(boxes[size_t(run.box[size_t(i)])], sideways);
                    run.error = std::max(run.error, std::fabs(p - (run.start + run.step * float(i))));
                }
                if (run.error > run.step * 0.4f) continue;  // not evenly spaced
            }
            if (run.found > best.found || (run.found == best.found && run.error < best.error)) best = run;
        }
        // one item: its length must be on screen once
        if (with_text == 1 && starts != 1) return false;
    }
    return best.found >= need;
}

// what is drawn on the row to the right of its text: the value, its arrows or a slider
Box ValueOf(const Box& text, const Box& row, const Screen& screen) {
    Box v{1e9f, 1e9f, -1e9f, -1e9f, 0};
    float h = Height(row);
    auto take = [&](const Box& b) {
        float cy = (b.y0 + b.y1) / 2;
        if (cy < row.y0 || cy >= row.y1 || b.x0 < text.x1 - 2 || Height(b) > h * 1.6f) return;
        if (b.x1 > g_frame_w || Width(b) > g_frame_w * 0.6f) return;
        v.x0 = std::min(v.x0, b.x0);
        v.y0 = std::min(v.y0, b.y0);
        v.x1 = std::max(v.x1, b.x1);
        v.y1 = std::max(v.y1, b.y1);
    };
    for (const Box& b : screen.texts) take(b);
    for (const Shape& s : screen.shapes) take(s.box);
    if (v.x1 <= v.x0) return Box{0, 0, 0, 0, 0};
    return v;
}

bool PlaceList(const List& list, const Screen& screen, std::vector<Spot>& out) {
    const std::vector<Box>& boxes = screen.texts;
    Run run;
    bool sideways = false;
    if (!Chain(list, boxes, false, run)) {
        sideways = true;
        if (!Chain(list, boxes, true, run)) return false;
    }
    int n = int(run.box.size()), with_text = 0;
    for (uint32_t len : list.lengths) with_text += len != 0;
    // the found texts' extent across the list, and their size
    float lo = 1e9f, hi = -1e9f, size = 0;
    for (int i = 0; i < n; i++) {
        if (run.box[size_t(i)] < 0) continue;
        const Box& b = boxes[size_t(run.box[size_t(i)])];
        lo = std::min(lo, sideways ? b.y0 : b.x0);
        hi = std::max(hi, sideways ? b.y1 : b.x1);
        size = std::max(size, Height(b));
    }
    float half = run.found > 1 ? run.step / 2 : size * 0.9f;
    for (int i = 0; i < n; i++) {
        if (!list.lengths[size_t(i)]) continue;
        Spot s;
        float centre;
        if (run.box[size_t(i)] >= 0) {
            s.text = boxes[size_t(run.box[size_t(i)])];
            centre = Along(s.text, sideways);
        } else if (run.found > 1) {
            centre = run.start + run.step * float(i);
            s.text = sideways ? Box{centre - half * 0.8f, lo, centre + half * 0.8f, hi, 0}
                              : Box{lo, centre - size / 2, hi, centre + size / 2, 0};
        } else {
            continue;
        }
        if (sideways) s.rect = Box{centre - half, lo - size * 0.8f, centre + half, hi + size * 0.8f, 0};
        else s.rect = Box{s.text.x0 - size * 2.4f, centre - half, std::max(hi, s.text.x1) + size * 0.8f, centre + half, 0};
        if (!sideways && run.box[size_t(i)] >= 0) {
            Box row{s.text.x0, centre - half, g_frame_w, centre + half, 0};
            s.value = ValueOf(s.text, row, screen);
            if (s.value.x1 > s.value.x0) s.rect.x1 = std::max(s.rect.x1, s.value.x1 + size * 0.5f);
        }
        s.arrows = list.arrows[size_t(i)];
        s.object = list.address;
        s.index = list.first + i;
        s.sideways = sideways;
        s.found = float(run.found) / float(with_text) + float(with_text) * 0.001f;
        out.push_back(s);
    }
    // a scrolled list: the place just above its first shown item is the item before it, and the list scrolls up to
    // show that one when the mouse goes there, as it scrolls down past its last
    if (!sideways && run.found > 1 && list.first > 0) {
        Spot s;
        float centre = run.start - run.step;
        s.text = Box{lo, centre - size / 2, hi, centre + size / 2, 0};
        s.rect = Box{lo - size * 2.4f, centre - half, hi + size * 0.8f, centre + half, 0};
        s.object = list.address;
        s.index = list.first - 1;
        s.found = float(run.found) / float(with_text) + float(with_text) * 0.001f;
        out.push_back(s);
    }
    return true;
}

// the grid's cells: the one mesh drawn once a cell, in rows down the screen
bool PlaceGrid(const Grid& grid, const Screen& screen, std::vector<Spot>& out) {
    std::map<std::pair<const void*, uint32_t>, std::vector<Box>> groups;
    for (const Shape& s : screen.shapes) {
        std::vector<Box>& g = groups[{s.mesh, s.count}];
        bool again = false;
        for (const Box& b : g) again |= std::fabs(b.x0 - s.box.x0) < 3 && std::fabs(b.y0 - s.box.y0) < 3;
        if (!again) g.push_back(s.box);
    }
    const std::vector<Box>* cells = nullptr;
    float best_area = 0;
    for (auto& [key, g] : groups) {
        if (int(g.size()) != grid.count) continue;
        float area = 0;
        for (const Box& b : g) area += Width(b) * Height(b);
        if (area > best_area) {
            best_area = area;
            cells = &g;
        }
    }
    if (!cells) return false;
    // rows by height on the screen, then left to right: the grid's own order
    std::vector<Box> sorted = *cells;
    float h = Height(sorted[0]);
    std::sort(sorted.begin(), sorted.end(), [&](const Box& a, const Box& b) {
        if (std::fabs(a.y0 - b.y0) > h * 0.5f) return a.y0 < b.y0;
        return a.x0 < b.x0;
    });
    for (int i = 0; i < grid.count; i++) {
        Spot s;
        s.rect = s.text = sorted[size_t(i)];
        s.grid = true;
        s.object = grid.address;
        s.index = i;
        s.found = 1;
        out.push_back(s);
    }
    return true;
}

// ---- the mouse ----

std::atomic<int> g_wheel{0};  // notches not yet used, up positive
std::atomic<int64_t> g_wheel_at{0};

// the cursor in the game's frame, as the presenter shows it (d3d12.cpp BlitFrame); false when it is not over the game
bool MouseInFrame(float& x, float& y, bool& left) {
    HWND hwnd = static_cast<HWND>(app::Window());
    left = false;
    if (GetForegroundWindow() != hwnd) return false;
    POINT at{};
    RECT client{};
    if (!GetCursorPos(&at) || !ScreenToClient(hwnd, &at) || !GetClientRect(hwnd, &client)) return false;
    float w = float(client.right), h = float(client.bottom);
    if (w <= 0 || h <= 0 || !PtInRect(&client, at)) return false;
    float aspect = app::DisplayAspect();
    float vw = std::min(w, h * aspect), vh = vw / aspect;
    float o = float(app::Get().overscan) / 1000.0f;
    float u = (float(at.x) - (w - vw) / 2) / vw * (1 - 2 * o) + o, v = (float(at.y) - (h - vh) / 2) / vh * (1 - 2 * o) + o;
    x = u * g_frame_w;
    y = v * g_frame_h;
    left = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    return true;
}

// what the mouse wants and the presses that get it there: controller 1's poll only
enum class Then { Nothing, A, Left, Right };
struct Steer {
    uint32_t object = 0;
    bool grid = false;
    int index = -1;        // the item or cell to select
    bool sideways = false;
    Then then = Then::Nothing;  // pressed once it is selected
    int repeat = 0;             // ...this many times (the wheel's notches)
    int tries = 0, last_selected = -1;
    enum { Idle, Hold, Gap } tap = Idle;
    uint16_t buttons = 0;  // the D-pad bit held
    bool a = false;
    int64_t since = 0;
};
Steer g_steer;
float g_last_x = -1, g_last_y = -1;
bool g_last_left = false;
int64_t g_checked_at = 0;
std::atomic<bool> g_active{false};

constexpr int64_t kHoldMs = 50, kGapMs = 50;  // a press the game's 30 a second polls cannot miss
constexpr int kTries = 4;                     // presses that change nothing before giving up

// a click that found nothing, the first few times: what was on screen and what the lists and grids hold, for the log
void TellMiss(float x, float y, const Screen& screen, const std::vector<uint32_t>& lists,
              const std::vector<uint32_t>& grids) {
    static int told = 0;
    if (told >= 40 || screen.texts.empty()) return;
    told++;
    std::string texts;
    for (const Box& b : screen.texts) {
        char t[64];
        std::snprintf(t, sizeof t, " %u@%.0f,%.0f-%.0f,%.0f", b.chars / 4, b.x0, b.y0, b.x1, b.y1);
        texts += t;
    }
    LOGR("menu mouse: click at %.0f, %.0f on nothing; %u lists and %u grids known, %u texts on screen:%s", x, y,
         uint32_t(lists.size()), uint32_t(grids.size()), uint32_t(screen.texts.size()), texts.c_str());
    std::vector<uint32_t> counts;
    for (const Box& b : screen.texts) counts.push_back(b.chars / 4);
    std::sort(counts.begin(), counts.end());
    int listed = 0;
    for (uint32_t address : lists) {
        List list;
        if (!ReadList(address, list) || listed >= 10) continue;
        size_t with_text = 0, seen = 0;
        std::string lengths;
        for (uint32_t n : list.lengths) {
            lengths += " " + std::to_string(n);
            with_text += n != 0;
            seen += n && std::binary_search(counts.begin(), counts.end(), n);
        }
        if (!with_text || seen * 2 < with_text) continue;
        listed++;
        std::vector<Spot> spots;
        LOGR("menu mouse:   list %08X first %d selected %d, lengths%s: %u of %u on screen, %s", address, list.first,
             list.selected, lengths.c_str(), uint32_t(seen), uint32_t(with_text),
             PlaceList(list, screen, spots) ? "placed" : "not placed");
    }
    for (uint32_t address : grids) {
        uint32_t f[8] = {};
        Read32(address, f[0]);
        Read32(address + 0x5C, f[1]);
        Read32(address + 0x1C, f[2]);
        if (f[2]) {
            Read32(f[2], f[3]);
            Read32(f[2] + 0x0C, f[4]);
            Read32(f[2] + 0x10, f[5]);
            Read32(f[2] + 0x30, f[6]);
        }
        Grid grid;
        std::vector<Spot> spots;
        bool read = ReadGrid(address, grid);
        if (f[1] == 0xFFFFFFFFu && !read) continue;  // not in use
        LOGR("menu mouse:   grid %08X vtable %08X selected %d, holder %08X vtable %08X: %u cells at %08X in %u rows, %s",
             address, f[0], int(f[1]), f[2], f[3], f[5], f[4], f[6],
             !read ? "not readable" : PlaceGrid(grid, screen, spots) ? "placed" : "not placed");
    }
    // the meshes drawn several times: a grid's cells are one of them
    std::map<std::pair<const void*, uint32_t>, std::vector<Box>> groups;
    for (const Shape& s : screen.shapes) groups[{s.mesh, s.count}].push_back(s.box);
    int told_groups = 0;
    for (auto& [key, g] : groups) {
        if (g.size() < 3 || told_groups >= 14) continue;
        told_groups++;
        std::string at;
        for (size_t i = 0; i < g.size() && i < 24; i++) {
            char t[64];
            std::snprintf(t, sizeof t, " %.0f,%.0f-%.0f,%.0f", g[i].x0, g[i].y0, g[i].x1, g[i].y1);
            at += t;
        }
        LOGR("menu mouse:   a mesh of %u vertices drawn %u times:%s", key.second, uint32_t(g.size()), at.c_str());
    }
    LOGR("menu mouse:   %u shapes on screen in all", uint32_t(screen.shapes.size()));
}

// where the mouse is among the lists and grids on screen. Only when something it goes by has changed since it last
// looked (the mouse moved, clicked or scrolled, the screen or the known lists changed), or a quarter second has passed:
// copying the screen and matching every known list to it is the costly part.
void Look() {
    float x, y;
    bool left;
    bool over = MouseInFrame(x, y, left);
    bool pressed = over && left && !g_last_left;
    bool moved = over && (std::fabs(x - g_last_x) > 0.75f || std::fabs(y - g_last_y) > 0.75f);
    int64_t now = NowMs();
    bool wheeling = g_wheel.load() != 0;
    uint64_t sign;
    bool shown;
    {
        std::lock_guard<std::mutex> l(g_screen_lock);
        sign = g_shown_sign;
        shown = now - g_shown_at < 400;
    }
    uint32_t known = g_known_changes;
    static uint64_t last_sign = 0;
    static bool last_shown = false, last_over = false;
    static uint32_t last_known = 0;
    static int64_t last_look = 0;
    if (!pressed && !moved && !wheeling && left == g_last_left && over == last_over && sign == last_sign &&
        shown == last_shown && known == last_known && now - last_look < 250)
        return;
    last_sign = sign;
    last_shown = shown;
    last_over = over;
    last_known = known;
    last_look = now;
    g_last_left = left;
    int wheel = 0;
    if (now - g_wheel_at.load() < 250) wheel = g_wheel.exchange(0);
    else g_wheel = 0;
    Screen screen;
    {
        std::lock_guard<std::mutex> l(g_screen_lock);
        if (now - g_shown_at < 400) screen = g_shown;
    }
    std::vector<uint32_t> lists, grids;
    {
        std::lock_guard<std::mutex> l(g_known_lock);
        lists = g_lists;
        grids = g_grids;
    }
    std::vector<uint32_t> counts;  // the texts' lengths, for a quick no
    for (const Box& b : screen.texts) counts.push_back(b.chars / 4);
    std::sort(counts.begin(), counts.end());
    std::vector<Spot> spots;
    static std::set<uint32_t> told;
    for (uint32_t address : lists) {
        List list;
        if (screen.texts.empty() || !ReadList(address, list)) continue;
        // most lists are of menus not shown: most of the items' lengths must be among the texts
        int with_text = 0, seen = 0;
        for (uint32_t n : list.lengths) {
            with_text += n != 0;
            seen += n && std::binary_search(counts.begin(), counts.end(), n);
        }
        if (!with_text || seen * 5 < with_text * 3) continue;
        size_t before = spots.size();
        if (PlaceList(list, screen, spots) && told.size() < 200 && told.insert(address).second)
            LOGR("menu mouse: list %08X is on screen, %u of its items placed, laid out %s", address,
                 uint32_t(spots.size() - before), spots.back().sideways ? "across" : "down");
    }
    for (uint32_t address : grids) {
        Grid grid;
        if (screen.shapes.empty() || !ReadGrid(address, grid)) continue;
        if (PlaceGrid(grid, screen, spots) && told.size() < 200 && told.insert(address).second)
            LOGR("menu mouse: grid %08X is on screen, %d cells", address, grid.count);
    }
    const Spot* under = nullptr;
    bool whole = false;  // a list with every item found, or a grid: a menu is surely up
    for (const Spot& s : spots) {
        whole |= s.found >= 1;
        if (over && Inside(s.rect, x, y) && (!under || s.found > under->found)) under = &s;
    }
    // the click is the menu's, not a swing, over an item or while a menu is surely up; a list only partly found
    // (which the HUD's texts can look like) does not stop a swing elsewhere
    g_active = whole || under;
    if (pressed && !under) TellMiss(x, y, screen, lists, grids);
    if (over) {
        g_last_x = x;
        g_last_y = y;
    }
    // the mouse only leads when it moves, clicks or scrolls: resting on an item, it leaves the menu to the keys
    if (!moved && !pressed && !wheel) return;
    Steer& st = g_steer;
    if (!under) {
        if (st.then == Then::Nothing) st.index = -1;
        return;
    }
    if (st.object != under->object || st.index != under->index) {
        st.tries = 0;
        st.last_selected = -1;
        st.then = Then::Nothing;
        st.repeat = 0;
    }
    st.object = under->object;
    st.grid = under->grid;
    st.index = under->index;
    st.sideways = under->sideways;
    if (wheel && !under->grid) {
        st.then = wheel > 0 ? Then::Right : Then::Left;
        st.repeat = std::min(std::abs(wheel), 6);
    } else if (pressed) {
        st.then = Then::A;
        st.repeat = 1;
        float h = Height(under->text);
        if (!under->grid && !under->sideways) {
            if (under->value.x1 > under->value.x0 && x >= under->value.x0 - h * 0.3f) {
                // on the value: its left half is Left, its right half Right
                st.then = x < (under->value.x0 + under->value.x1) / 2 ? Then::Left : Then::Right;
            } else if ((under->arrows & 1) && x < under->text.x0 + h * 1.3f) {
                st.then = Then::Left;
            } else if ((under->arrows & 2) && x > under->text.x1 - h * 1.3f) {
                st.then = Then::Right;
            }
        }
    }
}

}  // namespace

void MenuDraw(const app::DrawProbe& d) {
    // the menus are drawn by vertex programs (fixed function draws' constants are some program's) and, but for 3D text
    // in a round, without a depth buffer: a round's hundreds of small 3D meshes a frame are not looked at
    if (!d.program) return;
    bool text = d.prim == kQuadList && !d.indices && d.count >= 4 && d.count % 4 == 0;
    if (!text && (d.depth || d.count < 3 || d.count > kShapeVertices)) return;
    if (d.stride < d.position + 12) return;
    const float* scale = d.constants[kScreenScale];
    const float* offset = d.constants[kScreenScale + 1];
    // only what is drawn to the whole frame, through a program that puts it there this way
    if (!(offset[0] > 100) || !(offset[1] > 100) || !(std::fabs(scale[0]) > 50)) return;
    Box b{1e9f, 1e9f, -1e9f, -1e9f, d.count};
    for (uint32_t i = 0; i < d.count; i++) {
        float p[3];
        std::memcpy(p, d.vertices + size_t(d.indices ? d.indices[i] : i) * d.stride + d.position, 12);
        float c[4];
        for (int r = 0; r < 4; r++) {
            const float* m = d.constants[kTransform + r];
            c[r] = p[0] * m[0] + p[1] * m[1] + p[2] * m[2] + m[3];
        }
        if (!(c[3] > 1e-4f)) return;
        float x = c[0] * scale[0] / c[3] + offset[0], y = c[1] * scale[1] / c[3] + offset[1];
        b.x0 = std::min(b.x0, x);
        b.x1 = std::max(b.x1, x);
        b.y0 = std::min(b.y0, y);
        b.y1 = std::max(b.y1, y);
    }
    if (!(b.x1 > b.x0) || !(b.y1 > b.y0)) return;
    std::lock_guard<std::mutex> l(g_screen_lock);
    if (d.frame != g_frame) {
        g_shown_sign = Sign(g_drawing);
        std::swap(g_shown, g_drawing);
        g_drawing.texts.clear();
        g_drawing.shapes.clear();
        g_frame = d.frame;
        g_shown_at = NowMs();
    }
    if (!text) {
        // a shape: something small enough to be a button's part, on the screen
        if (b.x1 < 0 || b.y1 < 0 || b.x0 > offset[0] * 2 || b.y0 > offset[1] * 2) return;
        if (Width(b) < 3 || Height(b) < 3) return;
        b.chars = 0;
        if (g_drawing.shapes.size() < 2048) g_drawing.shapes.push_back({b, d.vertices, d.count});
        return;
    }
    g_frame_w = offset[0] * 2;
    g_frame_h = offset[1] * 2;
    // a text and its shadow are one
    for (const Box& o : g_drawing.texts)
        if (o.chars == b.chars && std::fabs(o.x0 - b.x0) < 5 && std::fabs(o.y0 - b.y0) < 5) return;
    if (g_drawing.texts.size() < 512) g_drawing.texts.push_back(b);
}

namespace {
void Note(std::vector<uint32_t>& known, uint32_t object) {
    g_known_changes++;
    std::lock_guard<std::mutex> l(g_known_lock);
    if (!known.empty() && known.front() == object) return;
    known.erase(std::remove(known.begin(), known.end(), object), known.end());
    known.insert(known.begin(), object);
    if (known.size() > kKnown) known.pop_back();
}
void Forget(std::vector<uint32_t>& known, uint32_t object) {
    g_known_changes++;
    std::lock_guard<std::mutex> l(g_known_lock);
    known.erase(std::remove(known.begin(), known.end(), object), known.end());
}
}  // namespace

void NoteMenuList(uint32_t list) { Note(g_lists, list); }
void ForgetMenuList(uint32_t list) { Forget(g_lists, list); }
void NoteMenuGrid(uint32_t grid) { Note(g_grids, grid); }
void ForgetMenuGrid(uint32_t grid) { Forget(g_grids, grid); }

bool MenuMouseActive() { return g_active; }

namespace {
std::atomic<bool> g_typing{false};
std::atomic<int64_t> g_typing_at{0};  // when the name entry last said it is up
std::atomic<int> g_tap_a{0};  // 0 none, 1 asked for, 2 being pressed
std::mutex g_typed_lock;
std::vector<uint16_t> g_typed;
}  // namespace

void SetMenuTyping(bool typing) {
    if (typing) g_typing_at = NowMs();
    if (g_typing.exchange(typing) == typing) return;
    std::lock_guard<std::mutex> l(g_typed_lock);
    g_typed.clear();
}
// a name entry that went without its frame telling (its menu replaced some other way) does not leave the typing
// keys taken from the controller
bool MenuTyping() { return g_typing && NowMs() - g_typing_at < 500; }
bool NextTyped(uint16_t& character) {
    std::lock_guard<std::mutex> l(g_typed_lock);
    if (g_typed.empty()) return false;
    character = g_typed.front();
    g_typed.erase(g_typed.begin());
    return true;
}
void MenuTapA() { g_tap_a = 1; }
bool MenuTapBusy() { return g_tap_a != 0; }

bool MenuMessage(void*, uint32_t msg, uintptr_t wp, intptr_t, intptr_t*) {
    // what the keyboard types, by its own layout: printable characters and Backspace (Enter stays the A button)
    if (msg == WM_CHAR && MenuTyping()) {
        uint16_t c = uint16_t(wp);
        bool printable = (c >= 0x20 && c < 0x7F) || (c >= 0xA1 && c <= 0xFF);
        std::lock_guard<std::mutex> l(g_typed_lock);
        if ((printable || c == 8) && g_typed.size() < 64) g_typed.push_back(c);
    }
    if (msg == WM_MOUSEWHEEL) {
        int notches = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        if (notches) {
            g_wheel += notches;
            g_wheel_at = NowMs();
        }
    }
    return false;
}

void MenuMousePad(input::Pad& pad) {
    int64_t now = NowMs();
    Steer& s = g_steer;
    // a press under way: held, then let go, each long enough for the game to see
    if (s.tap == Steer::Hold) {
        pad.buttons |= s.buttons;
        if (s.a) pad.analog[0] = 0xFF;
        if (now - s.since >= kHoldMs) {
            s.tap = Steer::Gap;
            s.since = now;
        }
        return;
    }
    if (s.tap == Steer::Gap) {
        if (now - s.since >= kGapMs) {
            s.tap = Steer::Idle;
            if (g_tap_a == 2) g_tap_a = 0;
        }
        return;
    }
    // A for the name entry's own key, before anything the mouse wants
    if (g_tap_a == 1) {
        g_tap_a = 2;
        s.buttons = 0;
        s.a = true;
        s.tap = Steer::Hold;
        s.since = now;
        pad.analog[0] = 0xFF;
        return;
    }
    if (now - g_checked_at >= 12) {
        g_checked_at = now;
        Look();
    }
    if (s.index < 0) return;
    auto drop = [&] {
        s.index = -1;
        s.then = Then::Nothing;
        s.repeat = 0;
    };
    int selected = -1;
    Grid grid;
    if (s.grid) {
        if (!ReadGrid(s.object, grid) || s.index >= grid.count) return drop();
        selected = grid.selected;
    } else {
        List list;
        if (!ReadList(s.object, list)) return drop();
        selected = list.selected;
    }
    auto press = [&](uint16_t buttons, bool a) {
        s.buttons = buttons;
        s.a = a;
        s.tap = Steer::Hold;
        s.since = now;
        pad.buttons |= buttons;
        if (a) pad.analog[0] = 0xFF;
    };
    // D-pad up 1, down 2, left 4, right 8
    if (selected == s.index) {
        if (s.then == Then::Nothing || s.repeat <= 0) return drop();
        press(s.then == Then::Left ? 4 : s.then == Then::Right ? 8 : 0, s.then == Then::A);
        if (--s.repeat <= 0) drop();
        return;
    }
    // the selection has to follow the presses: a menu that is not taking input is left alone
    if (selected == s.last_selected && ++s.tries > kTries) return drop();
    if (selected != s.last_selected) s.tries = 0;
    s.last_selected = selected;
    if (s.grid) {
        int row = grid.row[size_t(selected)], want = grid.row[size_t(s.index)];
        if (row != want) press(want > row ? 2 : 1, false);
        else press(s.index > selected ? 8 : 4, false);
        return;
    }
    bool forward = s.index > selected;
    press(s.sideways ? (forward ? 8 : 4) : (forward ? 2 : 1), false);
}

}  // namespace links
