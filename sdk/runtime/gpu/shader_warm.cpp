// shader_warm.h: the shader cache's files, the recipe list and the background compiles
#include "gpu/shader_warm.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "app/app.h"
#include "core/log.h"

namespace gpu {

std::string SpriteSizeHlsl(std::string s) {
    const char* tc3 = "    o.tc3 = oT3;\n";
    size_t at = s.find(tc3);
    if (at == std::string::npos) {
        LOGE("d3d12: a vertex shader no longer fits SpriteSizeHlsl");
        return s;
    }
    s.replace(at, std::strlen(tc3), "    o.tc3 = oPts;\n");
    return s;
}

namespace warm {

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

// ---- the cache: shader_cache\<hash>.bin, a header and the compiled code ----

namespace {

struct CacheHeader {
    uint32_t magic;  // "XSC2": the compiler's flags are part of the version
    uint32_t source_size;
    uint64_t check;
    uint32_t code_size;
};
constexpr uint32_t kCacheMagic = 0x32435358;

fs::path CachePath(uint64_t hash) {
    char name[32];
    std::snprintf(name, sizeof name, "%016llx.bin", static_cast<unsigned long long>(hash));
    return app::ExeDir() / "shader_cache" / name;
}

}  // namespace

uint64_t ShaderHash(const std::string& src, const char* entry, const char* profile, uint64_t& check) {
    // one source can hold several entry points
    uint64_t profile_seed = Hash(profile, std::strlen(profile));
    uint64_t entry_seed = Hash(entry, std::strlen(entry), profile_seed);
    check = Hash(src.data(), src.size(), ~entry_seed);
    return Hash(src.data(), src.size(), entry_seed);
}

bool LoadCached(uint64_t hash, uint64_t check, size_t source_size, ComPtr<ID3DBlob>& code) {
    FILE* f = _wfopen(CachePath(hash).c_str(), L"rb");
    if (!f) return false;
    CacheHeader h{};
    bool ok = std::fread(&h, sizeof h, 1, f) == 1 && h.magic == kCacheMagic && h.check == check &&
              h.source_size == uint32_t(source_size) && h.code_size > 0 && h.code_size < (16u << 20) &&
              SUCCEEDED(D3DCreateBlob(h.code_size, &code)) &&
              std::fread(code->GetBufferPointer(), 1, h.code_size, f) == h.code_size;
    std::fclose(f);
    if (!ok) code.Reset();
    return ok;
}

void StoreCached(uint64_t hash, uint64_t check, size_t source_size, ID3DBlob* code) {
    static std::once_flag made;
    static std::atomic<bool> logged{false};
    std::error_code ec;
    std::call_once(made, [] {
        std::error_code e;
        fs::create_directories(app::ExeDir() / "shader_cache", e);
    });
    // written under another name first, one per thread: a file half written when the game is closed is never read as
    // a shader
    fs::path path = CachePath(hash), part = path;
    part += L"." + std::to_wstring(GetCurrentThreadId()) + L".part";
    FILE* f = _wfopen(part.c_str(), L"wb");
    if (!f) {
        if (!logged.exchange(true))
            LOGW("shaders: the shader cache cannot be written in %s", path.parent_path().string().c_str());
        return;
    }
    CacheHeader h{kCacheMagic, uint32_t(source_size), check, uint32_t(code->GetBufferSize())};
    bool ok = std::fwrite(&h, sizeof h, 1, f) == 1 &&
              std::fwrite(code->GetBufferPointer(), 1, h.code_size, f) == h.code_size;
    std::fclose(f);
    if (ok) fs::rename(part, path, ec);
    if (!ok || ec) fs::remove(part, ec);
}

// ---- the list: shaders.list, a header and a record per shader ----

namespace {

enum : uint8_t { kPixel = 1, kProgram = 2, kFixed = 3 };

struct ListHeader {
    uint32_t magic;  // "XSL1"
    uint32_t pixel_key_size, fixed_key_size, record_size;
};
constexpr uint32_t kListMagic = 0x314C5358;

// what a shader was generated from; a pixel or fixed function key follows it in the file
struct Record {
    uint8_t kind = 0, quad = 0, sprite = 0, pretransformed = 0;
    uint16_t regs = 0;
    uint16_t key_size = 0;
    uint64_t shader = 0;        // its cache name when it was recorded
    uint64_t decode = 0;
    uint64_t program_hash = 0;  // kProgram: of the title's program, which is not in the list
    uint32_t program_tokens = 0;
    uint32_t reserved = 0;
};

struct Job {
    Record r;
    PixelKey pixel;
    FixedKey fixed;
    std::vector<uint32_t> program;
};

std::mutex g_lock;
std::condition_variable g_wake;
std::deque<Job> g_jobs;
std::unordered_set<uint64_t> g_known;  // shaders the list has
std::unordered_map<uint64_t, std::vector<Record>> g_waiting;  // a program's recipes, until the title loads it
std::unordered_set<uint64_t> g_programs;                       // programs seen this run
std::unordered_map<uint64_t, std::vector<uint32_t>> g_program_code;
bool g_started = false;
int g_busy = 0;
std::atomic<uint32_t> g_compiled{0}, g_cached{0}, g_failed{0}, g_changed{0};
std::chrono::steady_clock::time_point g_began;

fs::path ListPath() { return app::ExeDir() / "shaders.list"; }

uint64_t ProgramHash(const std::vector<uint32_t>& program) { return Hash(program.data(), program.size() * 4); }

void Append(const Record& r, const void* key) {
    FILE* f = _wfopen(ListPath().c_str(), L"ab");
    if (!f) return;
    std::fseek(f, 0, SEEK_END);
    if (std::ftell(f) == 0) {
        ListHeader h{kListMagic, uint32_t(sizeof(PixelKey)), uint32_t(sizeof(FixedKey)), uint32_t(sizeof(Record))};
        std::fwrite(&h, sizeof h, 1, f);
    }
    std::fwrite(&r, sizeof r, 1, f);
    if (r.key_size) std::fwrite(key, 1, r.key_size, f);
    std::fclose(f);
}

// the shader a recipe makes, as the renderer makes it at a draw
std::string Source(const Job& j, const char*& profile) {
    profile = j.r.kind == kPixel ? "ps_5_0" : "vs_5_0";
    if (j.r.kind == kPixel) return PixelHlsl(j.pixel);
    std::string src;
    if (j.r.kind == kProgram) {
        src = VertexProgramHlsl(j.program, j.r.regs, j.r.decode);
        if (j.r.sprite) src = SpriteSizeHlsl(std::move(src));
    } else {
        src = FixedVertexHlsl(j.r.regs, j.r.pretransformed != 0, j.r.decode, j.fixed);
    }
    src = ReversedDepthHlsl(std::move(src));
    return j.r.quad ? QuadHlsl(std::move(src)) : src;
}

void Run(const Job& j) {
    const char* profile = "";
    std::string src = Source(j, profile);
    uint64_t check = 0, hash = ShaderHash(src, "main", profile, check);
    if (hash != j.r.shader) g_changed++;  // generated differently by this runtime: still the shader a draw will ask for
    ComPtr<ID3DBlob> code, err;
    if (LoadCached(hash, check, src.size(), code)) {
        g_cached++;
        return;
    }
    if (FAILED(D3DCompile(src.data(), src.size(), nullptr, nullptr, nullptr, "main", profile,
                          D3DCOMPILE_OPTIMIZATION_LEVEL1, 0, &code, &err))) {
        g_failed++;
        return;
    }
    StoreCached(hash, check, src.size(), code.Get());
    g_compiled++;
}

void Worker() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    for (;;) {
        Job j;
        {
            std::unique_lock<std::mutex> l(g_lock);
            g_wake.wait(l, [] { return !g_jobs.empty(); });
            j = std::move(g_jobs.front());
            g_jobs.pop_front();
            g_busy++;
        }
        Run(j);
        std::lock_guard<std::mutex> l(g_lock);
        g_busy--;
        // a batch is done: what it came to, once there was something to compile
        if (g_jobs.empty() && !g_busy && g_compiled.load()) {
            double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_began).count();
            LOGR("shaders: %u compiled in the background, %u were in the cache, %u failed, %u generated anew "
                 "(%.1f s since the first)",
                 g_compiled.exchange(0), g_cached.exchange(0), g_failed.load(), g_changed.load(), s);
        }
    }
}

// under g_lock
void Queue(Job j) {
    if (g_jobs.empty() && !g_busy) g_began = std::chrono::steady_clock::now();
    g_jobs.push_back(std::move(j));
    g_wake.notify_one();
}

void ReadList() {
    FILE* f = _wfopen(ListPath().c_str(), L"rb");
    if (!f) return;
    ListHeader h{};
    bool fits = std::fread(&h, sizeof h, 1, f) == 1 && h.magic == kListMagic && h.pixel_key_size == sizeof(PixelKey) &&
                h.fixed_key_size == sizeof(FixedKey) && h.record_size == sizeof(Record);
    uint32_t pixel = 0, fixed = 0, program = 0;
    while (fits) {
        Job j;
        if (std::fread(&j.r, sizeof j.r, 1, f) != 1) break;
        size_t want = j.r.kind == kPixel ? sizeof(PixelKey) : j.r.kind == kFixed ? sizeof(FixedKey) : 0;
        if (j.r.kind < kPixel || j.r.kind > kFixed || j.r.key_size != want) break;  // cut short or damaged: stop here
        void* key = j.r.kind == kPixel ? static_cast<void*>(&j.pixel) : static_cast<void*>(&j.fixed);
        if (want && std::fread(key, 1, want, f) != want) break;
        if (!g_known.insert(j.r.shader).second) continue;
        if (j.r.kind == kProgram) {
            g_waiting[j.r.program_hash].push_back(j.r);
            program++;
        } else {
            (j.r.kind == kPixel ? pixel : fixed)++;
            Queue(std::move(j));
        }
    }
    std::fclose(f);
    if (!fits) {
        // another runtime's list: its keys no longer mean what they did, a new list starts
        std::error_code ec;
        fs::rename(ListPath(), fs::path(ListPath()) += L".old", ec);
        LOGR("shaders: shaders.list is from another version of the runtime, a new one starts");
        return;
    }
    LOGR("shaders: shaders.list has %u pixel, %u fixed function and %u vertex program shaders", pixel, fixed, program);
}

// a newly needed shader into the list
void Record_(Record r, const void* key) {
    std::lock_guard<std::mutex> l(g_lock);
    if (!g_started || !g_known.insert(r.shader).second) return;
    Append(r, key);
}

}  // namespace

void Start() {
    std::lock_guard<std::mutex> l(g_lock);
    if (g_started) return;
    g_started = true;
    ReadList();
    unsigned n = std::clamp(std::thread::hardware_concurrency() / 2, 1u, 6u);
    for (unsigned i = 0; i < n; i++) std::thread(Worker).detach();
    // programs the title made before the renderer started
    for (auto& [hash, code] : g_program_code) {
        auto it = g_waiting.find(hash);
        if (it == g_waiting.end()) continue;
        for (const Record& r : it->second) {
            Job j;
            j.r = r;
            j.program = code;
            Queue(std::move(j));
        }
        g_waiting.erase(it);
    }
    g_program_code.clear();
}

void NoteProgram(const std::vector<uint32_t>& program) {
    if (program.empty()) return;
    uint64_t hash = ProgramHash(program);
    std::lock_guard<std::mutex> l(g_lock);
    if (!g_programs.insert(hash).second) return;
    if (!g_started) {
        g_program_code[hash] = program;
        return;
    }
    auto it = g_waiting.find(hash);
    if (it == g_waiting.end()) return;
    for (const Record& r : it->second) {
        Job j;
        j.r = r;
        j.program = program;
        Queue(std::move(j));
    }
    g_waiting.erase(it);
}

void RecordPixel(uint64_t shader, const PixelKey& key) {
    Record r;
    r.kind = kPixel;
    r.shader = shader;
    r.key_size = uint16_t(sizeof key);
    Record_(r, &key);
}

void RecordProgram(uint64_t shader, const std::vector<uint32_t>& program, uint16_t regs, uint64_t decode, bool sprite,
                   bool quad) {
    Record r;
    r.kind = kProgram;
    r.shader = shader;
    r.regs = regs;
    r.decode = decode;
    r.sprite = sprite;
    r.quad = quad;
    r.program_hash = ProgramHash(program);
    r.program_tokens = uint32_t(program.size());
    Record_(r, nullptr);
}

void RecordFixed(uint64_t shader, uint16_t regs, bool pretransformed, uint64_t decode, const FixedKey& key, bool quad) {
    Record r;
    r.kind = kFixed;
    r.shader = shader;
    r.regs = regs;
    r.decode = decode;
    r.pretransformed = pretransformed;
    r.quad = quad;
    r.key_size = uint16_t(sizeof key);
    Record_(r, &key);
}

}  // namespace warm
}  // namespace gpu
