// Custom soundtracks from a folder on the PC.
//
// On the console the dashboard rips CDs into E:\TDATA\FFFE0000\MUSIC: an ST.DB index plus one WMA file per song, and
// the game's own XAPI code (XFindFirstSoundtrack 0x2BCC80, XFindNextSoundtrack 0x2BCC61, XGetSoundtrackSongInfo
// 0x2BCD37, XOpenSoundtrackSong 0x2BCF5F) reads them and its WMA decoder plays them. There is no dashboard here, so
// this builds the same thing from the "soundtracks" folder beside the exe:
//
//   soundtracks\Album name\song.mp3      one folder per soundtrack (up to 100), its songs in name order (up to 504)
//   soundtracks\song.mp3                 loose songs make a soundtrack called "Music"
//
// Each song (mp3, wma, m4a, aac, flac, wav) is converted once with Windows Media Foundation to WMA, the format the
// game's decoder reads, and kept under the hard disk's E folder; a later start converts only new or changed songs.
// The game reads the list once as it starts, so new songs are converted before boot, with a progress window.
//
// ST.DB, as the XAPI code reads it (little endian, 0x200 byte records):
//   0x0000 header: 1, soundtrack count, next soundtrack id, soundtrack ids[100], next song id (+0x19C)
//   (i + 1) * 0x200, soundtrack i: 0x21371, id, song count, song group ids[84], total ms (+0x15C), name WCHAR[32]
//   (group + 0x65) * 0x200, song group: 0x31073, soundtrack id, group id, 0, song ids[6] (+0x10), ms[6] (+0x28),
//                                       names 6 x WCHAR[32] (+0x40)
//   song file: MUSIC\%04x\%08x.WMA, the folder from the song id's high 16 bits, the file from the whole id
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/app.h"
#include "core/log.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "ole32.lib")

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace {

constexpr uint32_t kMaxSoundtracks = 100;
constexpr uint32_t kMaxGroups = 84;
constexpr uint32_t kMaxSongs = kMaxGroups * 6;
constexpr uint32_t kRecord = 0x200;
constexpr uint32_t kTargetBytesPerSecond = 16000;  // 128 kbit/s, what the dashboard rips at

struct Song {
    fs::path source;
    std::wstring name;
    uint32_t id = 0;
    uint32_t ms = 0;
    bool ready = false;
};

struct Soundtrack {
    std::wstring name;
    uint32_t id = 0;
    std::vector<Song> songs;
};

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

bool IsAudio(const fs::path& p) {
    std::wstring e = p.extension().wstring();
    for (auto& c : e) c = wchar_t(towlower(c));
    return e == L".mp3" || e == L".wma" || e == L".m4a" || e == L".aac" || e == L".flac" || e == L".wav";
}

std::vector<fs::path> SortedEntries(const fs::path& dir, bool dirs) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (dirs ? e.is_directory(ec) : (e.is_regular_file(ec) && IsAudio(e.path()))) out.push_back(e.path());
    }
    std::sort(out.begin(), out.end(), [](const fs::path& a, const fs::path& b) {
        return _wcsicmp(a.filename().c_str(), b.filename().c_str()) < 0;
    });
    return out;
}

// the 4 byte length of a song in milliseconds, from its media source
uint32_t DurationMs(IMFSourceReader* r) {
    PROPVARIANT v;
    PropVariantInit(&v);
    uint32_t ms = 0;
    if (SUCCEEDED(r->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &v)))
        ms = uint32_t(v.uhVal.QuadPart / 10000);
    PropVariantClear(&v);
    return ms;
}

// a WMA (Windows Media Audio 9 standard, the bit stream the console's decoder reads) type for this rate and channel
// count, near 128 kbit/s
ComPtr<IMFMediaType> WmaType(uint32_t rate, uint32_t channels) {
    ComPtr<IMFCollection> types;
    if (FAILED(MFTranscodeGetAudioOutputAvailableTypes(MFAudioFormat_WMAudioV8, MFT_ENUM_FLAG_ALL, nullptr, &types)))
        return nullptr;
    DWORD n = 0;
    types->GetElementCount(&n);
    ComPtr<IMFMediaType> best;
    uint32_t best_diff = UINT32_MAX;
    for (DWORD i = 0; i < n; i++) {
        ComPtr<IUnknown> u;
        ComPtr<IMFMediaType> t;
        if (FAILED(types->GetElement(i, &u)) || FAILED(u.As(&t))) continue;
        UINT32 r = 0, c = 0, bps = 0;
        t->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &r);
        t->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &c);
        t->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &bps);
        if (r != rate || c != channels) continue;
        uint32_t diff = bps > kTargetBytesPerSecond ? bps - kTargetBytesPerSecond : kTargetBytesPerSecond - bps;
        if (diff < best_diff) {
            best_diff = diff;
            best = t;
        }
    }
    return best;
}

// one song to WMA; its length in ms
bool Convert(const fs::path& in, const fs::path& out, uint32_t& ms) {
    ComPtr<IMFSourceReader> r;
    if (FAILED(MFCreateSourceReaderFromURL(in.c_str(), nullptr, &r))) {
        LOGW("soundtracks: %s cannot be read", Narrow(in.wstring()).c_str());
        return false;
    }
    ms = DurationMs(r.Get());
    r->SetStreamSelection(DWORD(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    r->SetStreamSelection(DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE);

    // decoded to 16 bit PCM at the song's own rate; mono and stereo only, the console's soundtracks are stereo
    ComPtr<IMFMediaType> native;
    UINT32 rate = 44100, channels = 2;
    if (SUCCEEDED(r->GetNativeMediaType(DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, &native))) {
        native->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
        native->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
    }
    channels = channels == 1 ? 1 : 2;
    ComPtr<IMFMediaType> pcm;
    MFCreateMediaType(&pcm);
    pcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    pcm->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
    pcm->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 2);
    pcm->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * channels * 2);
    pcm->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    if (FAILED(r->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, pcm.Get()))) {
        LOGW("soundtracks: %s cannot be decoded (%u Hz, %u channels)", Narrow(in.wstring()).c_str(), rate, channels);
        return false;
    }
    ComPtr<IMFMediaType> actual;
    if (FAILED(r->GetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &actual))) return false;
    actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
    actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);

    ComPtr<IMFMediaType> wma = WmaType(rate, channels);
    if (!wma) {
        LOGW("soundtracks: no WMA encoding for %s (%u Hz, %u channels)", Narrow(in.wstring()).c_str(), rate, channels);
        return false;
    }

    // written beside the final name and renamed at the end, so the game never opens half a file
    fs::path tmp = out;
    tmp += L".part";
    std::error_code ec;
    fs::remove(tmp, ec);
    ComPtr<IMFAttributes> attrs;
    MFCreateAttributes(&attrs, 1);
    attrs->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_ASF);
    ComPtr<IMFSinkWriter> w;
    if (FAILED(MFCreateSinkWriterFromURL(tmp.c_str(), nullptr, attrs.Get(), &w))) return false;
    DWORD stream = 0;
    if (FAILED(w->AddStream(wma.Get(), &stream)) || FAILED(w->SetInputMediaType(stream, actual.Get(), nullptr)) ||
        FAILED(w->BeginWriting())) {
        LOGW("soundtracks: the WMA encoder refused %s", Narrow(in.wstring()).c_str());
        return false;
    }
    bool ok = true;
    for (;;) {
        DWORD flags = 0;
        LONGLONG time = 0;
        ComPtr<IMFSample> s;
        if (FAILED(r->ReadSample(DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags, &time, &s))) {
            ok = false;
            break;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (s && FAILED(w->WriteSample(stream, s.Get()))) {
            ok = false;
            break;
        }
    }
    if (FAILED(w->Finalize())) ok = false;
    w.Reset();
    if (!ok) {
        fs::remove(tmp, ec);
        LOGW("soundtracks: converting %s failed", Narrow(in.wstring()).c_str());
        return false;
    }
    fs::remove(out, ec);
    fs::rename(tmp, out, ec);
    return !ec;
}

void Put32(std::vector<uint8_t>& db, size_t at, uint32_t v) { std::memcpy(db.data() + at, &v, 4); }

void PutName(std::vector<uint8_t>& db, size_t at, const std::wstring& name) {
    // WCHAR[32], null terminated
    size_t n = std::min<size_t>(name.size(), 31);
    for (size_t i = 0; i < n; i++) {
        uint16_t c = uint16_t(name[i]);
        std::memcpy(db.data() + at + i * 2, &c, 2);
    }
}

// ST.DB from the songs converted so far
void WriteDatabase(const fs::path& music, const std::vector<Soundtrack>& tracks) {
    std::vector<uint8_t> db(0x65 * kRecord, 0);  // grown below as groups are added
    uint32_t count = 0, end_group = 0;
    for (size_t t = 0; t < tracks.size(); t++) {
        const auto& st = tracks[t];
        std::vector<const Song*> ready;
        for (const auto& s : st.songs)
            if (s.ready) ready.push_back(&s);
        if (ready.empty()) continue;
        uint32_t slot = count++;
        Put32(db, 0xC + slot * 4, st.id);
        size_t rec = size_t(slot + 1) * kRecord;
        Put32(db, rec, 0x21371);
        Put32(db, rec + 4, st.id);
        Put32(db, rec + 8, uint32_t(ready.size()));
        uint32_t total = 0;
        for (size_t g = 0; g * 6 < ready.size(); g++) {
            // the XAPI keeps the last group read and takes it again for songs g * 6 to g * 6 + 5 of the same
            // soundtrack, as if a group's id were its place in its soundtrack. Each soundtrack's groups start at
            // its place times 84 (the most it has), so that never matches another of its groups
            uint32_t group = uint32_t(t) * kMaxGroups + uint32_t(g);
            end_group = std::max(end_group, group + 1);
            Put32(db, rec + 0xC + g * 4, group);
            size_t grec = size_t(group + 0x65) * kRecord;
            if (db.size() < grec + kRecord) db.resize(grec + kRecord, 0);
            Put32(db, grec, 0x31073);
            Put32(db, grec + 4, st.id);
            Put32(db, grec + 8, group);
            for (size_t k = 0; k < 6 && g * 6 + k < ready.size(); k++) {
                const Song& s = *ready[g * 6 + k];
                Put32(db, grec + 0x10 + k * 4, s.id);
                Put32(db, grec + 0x28 + k * 4, s.ms);
                PutName(db, grec + 0x40 + k * 0x40, s.name);
                total += s.ms;
            }
        }
        Put32(db, rec + 0x15C, total);
        PutName(db, rec + 0x160, st.name);
    }
    Put32(db, 0, 1);
    Put32(db, 4, count);
    Put32(db, 8, uint32_t(tracks.size()));
    Put32(db, 0x19C, 0);
    db.resize(std::max<size_t>(size_t(end_group + 0x65) * kRecord, 0x65 * kRecord));

    fs::path tmp = music / L"ST.DB.part", path = music / L"ST.DB";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(db.data()), std::streamsize(db.size()));
    }
    if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) return;
    // the game keeps ST.DB open (its first XFindFirstSoundtrack is never closed), and a file open in the game
    // cannot be replaced, only written over: the old list stayed, one song long when the game first read it
    std::error_code ec;
    fs::remove(tmp, ec);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        LOGW("soundtracks: ST.DB could not be written (%lu)", GetLastError());
        return;
    }
    DWORD done = 0;
    WriteFile(h, db.data(), DWORD(db.size()), &done, nullptr);
    SetEndOfFile(h);
    CloseHandle(h);
}

// what each converted file came from, so a later start converts only new or changed songs
using Manifest = std::map<std::wstring, std::wstring>;  // output file name -> source path|size|time

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

// UTF-8 text, a line per file: its name, a tab, where it came from
Manifest ReadManifest(const fs::path& p) {
    Manifest m;
    std::ifstream f(p, std::ios::binary);
    std::string line;
    while (std::getline(f, line)) {
        size_t tab = line.find('\t');
        if (tab != std::string::npos) m[Widen(line.substr(0, tab))] = Widen(line.substr(tab + 1));
    }
    return m;
}

void WriteManifest(const fs::path& p, const Manifest& m) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    for (auto& [k, v] : m) f << Narrow(k) << '\t' << Narrow(v) << '\n';
}

std::wstring Stamp(const fs::path& p) {
    std::error_code ec;
    auto size = fs::file_size(p, ec);
    auto time = fs::last_write_time(p, ec).time_since_epoch().count();
    return p.wstring() + L"|" + std::to_wstring(size) + L"|" + std::to_wstring(time);
}

// what the start-up window shows, from the converting thread
struct Progress {
    std::atomic<bool> planned{false}, finished{false}, cancel{false};
    std::atomic<size_t> todo{0}, done{0};
    std::mutex lock;
    std::wstring current;
};

// the hard disk's E folder, as main.cpp places it: hdd beside the exe, hdd2 and on for a second copy (--instance)
fs::path MusicFolder() {
    std::wstring hdd = app::Instance() > 1 ? L"hdd" + std::to_wstring(app::Instance()) : L"hdd";
    return app::ExeDir() / hdd / L"E" / L"TDATA" / L"FFFE0000" / L"MUSIC";
}

void Build(Progress& progress) {
    fs::path root = app::ExeDir() / L"soundtracks";
    std::error_code ec;
    fs::create_directories(root, ec);
    LOGR("soundtracks: music folder %s%s", Narrow(root.wstring()).c_str(), ec ? " could not be made" : "");
    fs::path music = MusicFolder();
    fs::create_directories(music, ec);

    std::vector<Soundtrack> tracks;
    std::vector<fs::path> loose = SortedEntries(root, false);
    if (!loose.empty()) tracks.push_back({L"Music", 0, {}});
    for (auto& f : loose) tracks.back().songs.push_back({f, f.stem().wstring()});
    for (auto& d : SortedEntries(root, true)) {
        if (tracks.size() >= kMaxSoundtracks) break;
        Soundtrack st{d.filename().wstring(), 0, {}};
        for (auto& f : SortedEntries(d, false)) st.songs.push_back({f, f.stem().wstring()});
        if (!st.songs.empty()) tracks.push_back(std::move(st));
    }
    size_t total = 0;
    for (uint32_t i = 0; i < tracks.size(); i++) {
        // ids from 1: the XAPI starts with a zeroed cache, so a soundtrack 0 would look already read, and empty
        tracks[i].id = i + 1;
        if (tracks[i].songs.size() > kMaxSongs) tracks[i].songs.resize(kMaxSongs);
        for (uint32_t k = 0; k < tracks[i].songs.size(); k++) tracks[i].songs[k].id = tracks[i].id << 16 | k;
        total += tracks[i].songs.size();
    }

    // folders of soundtracks that are gone
    for (auto& e : fs::directory_iterator(music, ec)) {
        if (!e.is_directory(ec)) continue;
        unsigned n = 0;
        if (std::swscanf(e.path().filename().c_str(), L"%x", &n) == 1 && (n == 0 || n > tracks.size()))
            fs::remove_all(e.path(), ec);
    }

    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET))) LOGW("soundtracks: Windows Media Foundation did not start");

    // which songs are converted already: the manifest keeps each one's source (path, size, time) and length
    fs::path manifest_path = music / L"sources.txt";
    Manifest manifest = ReadManifest(manifest_path);
    Manifest kept;
    struct Job {
        Song* song;
        fs::path out;
        std::wstring key, stamp;
    };
    std::vector<Job> jobs;
    size_t reused = 0, converted = 0, failed = 0;
    for (auto& st : tracks) {
        wchar_t dir[8];
        std::swprintf(dir, 8, L"%04x", st.id);
        fs::create_directories(music / dir, ec);
        for (auto& s : st.songs) {
            wchar_t file[16];
            std::swprintf(file, 16, L"%08x.WMA", s.id);
            fs::path out = music / dir / file;
            std::wstring key = std::wstring(dir) + L"\\" + file, stamp = Stamp(s.source);
            auto it = manifest.find(key);
            bool same = it != manifest.end() && (it->second == stamp || it->second.rfind(stamp + L"|ms ", 0) == 0);
            if (same && fs::exists(out, ec)) {
                if (it->second != stamp) {
                    s.ms = uint32_t(std::wcstoul(it->second.c_str() + stamp.size() + 4, nullptr, 10));
                } else {  // written before lengths were kept: from the WMA itself
                    ComPtr<IMFSourceReader> r;
                    if (SUCCEEDED(MFCreateSourceReaderFromURL(out.c_str(), nullptr, &r))) s.ms = DurationMs(r.Get());
                }
                s.ready = true;
                kept[key] = stamp + L"|ms " + std::to_wstring(s.ms);
                reused++;
            } else {
                jobs.push_back({&s, out, key, stamp});
            }
        }
    }
    progress.todo = jobs.size();
    progress.planned = true;
    if (total) {
        LOGR("soundtracks: %zu songs in %zu soundtracks, %zu to convert", total, tracks.size(), jobs.size());
    } else {
        LOGI("soundtracks: none, put music in %s (a folder per soundtrack)", Narrow(root.wstring()).c_str());
    }

    for (auto& j : jobs) {
        if (progress.cancel) break;
        {
            std::lock_guard<std::mutex> l(progress.lock);
            progress.current = j.song->source.stem().wstring();
        }
        if (Convert(j.song->source, j.out, j.song->ms)) {
            j.song->ready = true;
            kept[j.key] = j.stamp + L"|ms " + std::to_wstring(j.song->ms);
            converted++;
            LOGR("soundtracks: converted %zu of %zu, %s (%u:%02u)", converted + failed, jobs.size(),
                 Narrow(j.song->source.filename().wstring()).c_str(), j.song->ms / 60000, j.song->ms / 1000 % 60);
        } else {
            failed++;
        }
        progress.done = converted + failed;
    }

    // songs no longer listed
    for (auto& st : tracks) {
        wchar_t dir[8];
        std::swprintf(dir, 8, L"%04x", st.id);
        for (auto& e : fs::directory_iterator(music / dir, ec)) {
            std::wstring key = std::wstring(dir) + L"\\" + e.path().filename().wstring();
            if (!kept.count(key)) fs::remove(e.path(), ec);
        }
    }
    WriteDatabase(music, tracks);
    WriteManifest(manifest_path, kept);
    MFShutdown();
    if (SUCCEEDED(co)) CoUninitialize();
    if (total)
        LOGR("soundtracks: ready, %zu converted, %zu kept from before, %zu could not be converted%s", converted, reused,
             failed, progress.cancel ? ", the rest left for the next start" : "");
    progress.finished = true;
}

// ---- the start-up window, drawn like the one that prepares the game code (app/game_code.cpp): a heading, the song
// being converted, a bar, the share done and the time left ----

constexpr COLORREF kBackground = RGB(10, 11, 10);
constexpr COLORREF kText = RGB(227, 232, 219);
constexpr COLORREF kDim = RGB(145, 153, 138);
constexpr COLORREF kBarEmpty = RGB(28, 33, 26);
constexpr COLORREF kBarFull = RGB(145, 158, 130);

Progress* g_shown = nullptr;
int64_t g_started_ms = 0;

int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void FillBox(HDC dc, const RECT& r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

void DrawLine(HDC dc, RECT r, const std::wstring& text, COLORREF c, UINT align) {
    SetTextColor(dc, c);
    DrawTextW(dc, text.c_str(), int(text.size()), &r, align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

std::wstring TimeLeft(const Progress& p) {
    size_t done = p.done, todo = p.todo;
    if (!done || done >= todo) return L"";
    int64_t per = (NowMs() - g_started_ms) / int64_t(done);
    int64_t left = per * int64_t(todo - done) / 1000;
    if (left < 60) return L"about " + std::to_wstring(std::max<int64_t>(left, 1)) + L" s left";
    return L"about " + std::to_wstring((left + 30) / 60) + L" min left";
}

void Paint(HDC dc, const RECT& client, int unit) {
    const Progress& p = *g_shown;
    RECT heading{client.left + unit, client.top + unit, client.right - unit, client.top + unit * 3};
    RECT stage = heading;
    OffsetRect(&stage, 0, unit * 2);
    RECT bar{stage.left, stage.bottom + unit / 2, stage.right, stage.bottom + unit / 2 + unit};
    RECT bottom{bar.left, bar.bottom + unit / 2, bar.right, bar.bottom + unit / 2 + unit * 2};

    FillBox(dc, client, kBackground);
    SetBkMode(dc, TRANSPARENT);
    size_t done = p.done, todo = std::max<size_t>(p.todo, 1);
    std::wstring name;
    {
        std::lock_guard<std::mutex> l(g_shown->lock);
        name = g_shown->current;
    }
    DrawLine(dc, heading, L"Converting new songs for the game's soundtracks.", kText, DT_LEFT);
    std::wstring line = p.cancel ? L"Stopping after this song; the rest convert on the next start."
                                 : L"Song " + std::to_wstring(std::min(done + 1, todo)) + L" of " +
                                       std::to_wstring(todo) + L": " + name;
    DrawLine(dc, stage, line, kDim, DT_LEFT);
    FillBox(dc, bar, kBarEmpty);
    float fraction = float(done) / float(todo);
    RECT filled = bar;
    filled.right = bar.left + LONG(float(bar.right - bar.left) * fraction);
    FillBox(dc, filled, kBarFull);
    DrawLine(dc, bottom, std::to_wstring(int(fraction * 100.0f)) + L"%", kDim, DT_LEFT);
    DrawLine(dc, bottom, TimeLeft(p), kDim, DT_RIGHT);
}

LRESULT CALLBACK ProgressProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {  // painted off screen, then copied, so the bar does not flicker
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client;
            GetClientRect(hwnd, &client);
            HDC memory = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
            HGDIOBJ old_bitmap = SelectObject(memory, bitmap);
            HGDIOBJ old_font = SelectObject(memory, reinterpret_cast<HFONT>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)));
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
        case WM_CLOSE: {  // stops after the song being converted; the game starts with the songs done so far
            const wchar_t* question = L"Stop converting songs? The game starts with the ones done so far, and the next "
                                      L"start converts the rest.";
            if (!g_shown->cancel && MessageBoxW(hwnd, question, app::kGame.title, MB_YESNO | MB_ICONQUESTION) == IDYES)
                g_shown->cancel = true;
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// centred on the primary monitor, sized for its DPI, in the message font
void ShowProgress(Progress& progress) {
    g_shown = &progress;
    g_started_ms = NowMs();
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = ProgressProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
    std::wstring class_name = std::wstring(app::kGame.name) + L"Soundtracks";
    wc.lpszClassName = class_name.c_str();
    RegisterClassExW(&wc);
    UINT dpi = GetDpiForSystem();
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r{0, 0, MulDiv(560, dpi, 96), MulDiv(130, dpi, 96)};
    AdjustWindowRectExForDpi(&r, style, FALSE, WS_EX_TOPMOST, dpi);
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, app::kGame.title, style, x, y, w, h, nullptr, nullptr,
                                inst, nullptr);
    if (!hwnd) return;
    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi);
    HFONT font = CreateFontIndirectW(&metrics.lfMessageFont);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, LONG_PTR(font));
    SetTimer(hwnd, 1, 100, nullptr);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);

    while (!progress.finished) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
    }
    DestroyWindow(hwnd);
    DeleteObject(font);
    g_shown = nullptr;
}

}  // namespace

namespace links {

// GameInfo::window: the game window is up and boot follows. The game reads its soundtrack list once as it starts, so
// new songs are converted here first, with a progress window, and the list is complete when the game looks
void StartSoundtracks(void*) {
    static std::atomic<bool> started{false};
    if (started.exchange(true)) return;
    Progress progress;
    std::thread worker([&progress] { Build(progress); });
    // the plan (which songs are new) takes a moment; the window shows only when there is something to convert
    while (!progress.planned && !progress.finished) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (progress.todo) ShowProgress(progress);
    worker.join();
}

}  // namespace links
