#include "RekordboxLiveWatcher.h"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <chrono>

#ifdef _WIN32
  #include <Windows.h>
  #include <TlHelp32.h>
  #include <Psapi.h>
  #include <ShlObj.h>
  #pragma comment(lib, "Psapi.lib")
#elif defined(__APPLE__)
  #include <juce_core/juce_core.h>
#endif

namespace fs = std::filesystem;

namespace BeatMate::Services::Rekordbox {

using BeatMate::Services::DJSoftware::PlayedTrack;

namespace {

std::string toLowerAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char) std::tolower(c); });
    return s;
}

#if defined(_WIN32) || defined(__APPLE__)
bool parseWindowTitle(const std::string& title, PlayedTrack& out)
{
    std::string t = title;

    std::vector<std::string> parts;
    {
        const std::string sep = " - ";
        size_t start = 0, pos;
        while ((pos = t.find(sep, start)) != std::string::npos) {
            parts.push_back(t.substr(start, pos - start));
            start = pos + sep.size();
        }
        parts.push_back(t.substr(start));
    }

    static const std::vector<std::string> popupTitles = {
        // English
        "ableton link", "preferences", "master out", "mixer",
        "recorder", "bridge", "cloud library sync", "sampler",
        "my tag", "related tracks", "keyboard shortcuts",
        "notifications", "dropbox", "playlist bank",
        // French
        "préférences", "preferences", "enregistreur", "mélangeur",
        "aide", "paramètres", "échantillonneur", "raccourcis clavier",
        "notifications", "bibliothèque cloud",
        // German
        "einstellungen",
        // Spanish
        "preferencias",
        // Italian
        "preferenze",
        // Japanese (rough transliteration — Rekordbox JP UI)
        "環境設定"
    };
    std::vector<std::string> usable;
    for (auto& p : parts) {
        std::string lp = toLowerAscii(p);
        if (lp.rfind("rekordbox", 0) == 0) continue;
        auto l = p.find_first_not_of(" \t");
        auto r = p.find_last_not_of(" \t");
        if (l == std::string::npos) continue;
        std::string trimmed = p.substr(l, r - l + 1);
        std::string ltrim = toLowerAscii(trimmed);
        bool isPopup = false;
        for (const auto& popup : popupTitles) {
            if (ltrim == popup) { isPopup = true; break; }
        }
        if (isPopup) continue;
        usable.push_back(trimmed);
    }

    if (usable.size() >= 2) {
        out.artist = usable[0];
        out.title  = usable[1];
        return true;
    }
    // A single usable segment is a popup title, not a track.
    return false;
}
#endif

// Return candidate log file paths for the rekordbox "Pioneer" data folder.
//   Windows: %APPDATA%\Pioneer\rekordbox*\*.log
//   macOS:   ~/Library/Pioneer/rekordbox*/*.log
std::vector<fs::path> candidateLogs()
{
    std::vector<fs::path> out;

    fs::path pioneer;
#ifdef _WIN32
    wchar_t appData[MAX_PATH] = {};
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData) != S_OK)
        return out;
    pioneer = fs::path(appData) / L"Pioneer";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') return out;
    pioneer = fs::path(home) / "Library" / "Pioneer";
#endif
    if (pioneer.empty() || !fs::is_directory(pioneer)) return out;

    std::error_code ec;
    for (auto& entry : fs::directory_iterator(pioneer, ec)) {
        if (!entry.is_directory()) continue;
        auto name = entry.path().filename().string();
        std::string lname = toLowerAscii(name);
        if (lname.rfind("rekordbox", 0) != 0) continue;
        // Non-recursive: rekordbox keeps its logs at the top of each dir.
        for (auto& f : fs::directory_iterator(entry.path(), ec)) {
            if (!f.is_regular_file()) continue;
            auto ext = toLowerAscii(f.path().extension().string());
            if (ext == ".log") out.push_back(f.path());
        }
    }
    // Newest first - so readFromLogFile only tails the freshest one.
    std::sort(out.begin(), out.end(),
              [](const fs::path& a, const fs::path& b) {
                  std::error_code e1, e2;
                  auto ta = fs::last_write_time(a, e1);
                  auto tb = fs::last_write_time(b, e2);
                  return ta > tb;
              });
    return out;
}

// Read at most the last `tailBytes` bytes (rekordbox logs can be huge).
std::string tailFile(const fs::path& p, size_t tailBytes = 64 * 1024)
{
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in) return {};
    auto size = static_cast<std::streamoff>(in.tellg());
    if (size <= 0) return {};
    std::streamoff start = size > static_cast<std::streamoff>(tailBytes)
                             ? size - static_cast<std::streamoff>(tailBytes)
                             : 0;
    in.seekg(start, std::ios::beg);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // anonymous namespace

#ifdef _WIN32

namespace {

std::string wideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int need = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int) w.size(),
                                   nullptr, 0, nullptr, nullptr);
    if (need <= 0) return {};
    std::string out(static_cast<size_t>(need), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int) w.size(),
                        out.data(), need, nullptr, nullptr);
    return out;
}

std::string processNameForPid(DWORD pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    wchar_t buf[MAX_PATH] = {};
    DWORD len = MAX_PATH;
    std::string name;
    if (QueryFullProcessImageNameW(h, 0, buf, &len)) {
        std::wstring path(buf, len);
        auto pos = path.find_last_of(L"\\/");
        name = wideToUtf8(pos == std::wstring::npos ? path : path.substr(pos + 1));
    }
    CloseHandle(h);
    return name;
}

struct EnumCtx {
    std::string bestTitle;              // chosen window title (UTF-8)
    size_t      bestLen = 0;            // length of the chosen title (tie-breaker: longest)
    std::vector<std::string> allTitles; // every rekordbox window title seen (for diag)
};

static std::string readHwndTitle(HWND hwnd)
{
    int titleLen = GetWindowTextLengthW(hwnd);
    if (titleLen <= 0) return {};
    std::wstring wtitle(static_cast<size_t>(titleLen) + 1, L'\0');
    int got = GetWindowTextW(hwnd, wtitle.data(), titleLen + 1);
    if (got <= 0) return {};
    wtitle.resize(static_cast<size_t>(got));
    return wideToUtf8(wtitle);
}

// Also scan child windows. rekordbox 7 keeps the main caption as "rekordbox"
BOOL CALLBACK enumChildProc(HWND hwnd, LPARAM lParam)
{
    auto* ctx = reinterpret_cast<EnumCtx*>(lParam);
    std::string t = readHwndTitle(hwnd);
    if (t.size() < 4) return TRUE;
    ctx->allTitles.push_back(t);
    // Prefer titles that look like track info (contain " - " separator)
    bool looksLikeTrack = t.find(" - ") != std::string::npos;
    if (looksLikeTrack && t.size() > ctx->bestLen) {
        ctx->bestLen = t.size();
        ctx->bestTitle = t;
    } else if (!looksLikeTrack && ctx->bestTitle.empty()
               && t.size() > ctx->bestLen) {
        ctx->bestLen = t.size();
        ctx->bestTitle = t;
    }
    return TRUE;
}

BOOL CALLBACK enumProc(HWND hwnd, LPARAM lParam)
{
    auto* ctx = reinterpret_cast<EnumCtx*>(lParam);
    if (!IsWindowVisible(hwnd)) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return TRUE;

    std::string exe = toLowerAscii(processNameForPid(pid));
    if (exe.find("rekordbox") == std::string::npos) return TRUE;

    std::string title = readHwndTitle(hwnd);
    if (title.size() >= 4) {
        ctx->allTitles.push_back(title);
        bool looksLikeTrack = title.find(" - ") != std::string::npos;
        if (looksLikeTrack && title.size() > ctx->bestLen) {
            ctx->bestLen = title.size();
            ctx->bestTitle = title;
        } else if (!looksLikeTrack && ctx->bestTitle.empty()
                   && title.size() > ctx->bestLen) {
            ctx->bestLen = title.size();
            ctx->bestTitle = title;
        }
    }
    EnumChildWindows(hwnd, enumChildProc, lParam);
    return TRUE;
}

} // anonymous namespace

std::optional<PlayedTrack> RekordboxLiveWatcher::readFromWindowTitle()
{
    EnumCtx ctx;
    EnumWindows(enumProc, reinterpret_cast<LPARAM>(&ctx));
    if (!ctx.allTitles.empty()) {
        std::string joined;
        for (auto& t : ctx.allTitles) { joined += "  | "; joined += t; }
        spdlog::debug("[RBLive] window-title: {} rekordbox titles seen:{}",
                      ctx.allTitles.size(), joined);
    }
    if (ctx.bestTitle.empty()) {
        spdlog::debug("[RBLive] window-title: no rekordbox window found");
        return std::nullopt;
    }

    PlayedTrack pt;
    pt.source = "Rekordbox";
    if (!parseWindowTitle(ctx.bestTitle, pt)) {
        spdlog::debug("[RBLive] window-title: '{}' did not yield track info",
                      ctx.bestTitle);
        return std::nullopt;
    }

    // playedAtUnix = now (the window is currently showing this track).
    pt.playedAtUnix = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    std::string sig = pt.artist + "||" + pt.title;
    if (sig != lastSignature_) {
        spdlog::info("[RBLive] window-title hit: artist='{}' title='{}' (raw='{}')",
                     pt.artist, pt.title, ctx.bestTitle);
        lastSignature_ = sig;
    } else {
        spdlog::debug("[RBLive] window-title unchanged: {}", sig);
    }
    return pt;
}

#elif defined(__APPLE__)

std::optional<PlayedTrack> RekordboxLiveWatcher::readFromWindowTitle()
{
    juce::StringArray cmd;
    cmd.add("osascript");
    cmd.add("-e");
    cmd.add("tell application \"System Events\" to get name of front window "
            "of process \"rekordbox\"");

    juce::ChildProcess proc;
    if (!proc.start(cmd)) {
        spdlog::debug("[RBLive] window-title: failed to launch osascript");
        return std::nullopt;
    }

    juce::String output = proc.readAllProcessOutput().trim();
    if (output.isEmpty()) {
        spdlog::debug("[RBLive] window-title: rekordbox not running or no front window");
        return std::nullopt;
    }
    // osascript prints AppleScript failures (process not found, no window,
    // automation not authorized) as "... execution error: ...".
    if (output.containsIgnoreCase("execution error")) {
        spdlog::debug("[RBLive] window-title: osascript error: {}",
                      output.toStdString());
        return std::nullopt;
    }

    std::string bestTitle = output.toStdString();

    PlayedTrack pt;
    pt.source = "Rekordbox";
    if (!parseWindowTitle(bestTitle, pt)) {
        spdlog::debug("[RBLive] window-title: '{}' did not yield track info",
                      bestTitle);
        return std::nullopt;
    }

    // playedAtUnix = now (the window is currently showing this track).
    pt.playedAtUnix = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    std::string sig = pt.artist + "||" + pt.title;
    if (sig != lastSignature_) {
        spdlog::info("[RBLive] window-title hit: artist='{}' title='{}' (raw='{}')",
                     pt.artist, pt.title, bestTitle);
        lastSignature_ = sig;
    } else {
        spdlog::debug("[RBLive] window-title unchanged: {}", sig);
    }
    return pt;
}

#else

std::optional<PlayedTrack> RekordboxLiveWatcher::readFromWindowTitle() { return std::nullopt; }

#endif

std::optional<PlayedTrack> RekordboxLiveWatcher::readFromLogFile()
{
    auto logs = candidateLogs();
    if (logs.empty()) {
        spdlog::debug("[RBLive] log-file: no rekordbox log found under the Pioneer folder");
        return std::nullopt;
    }
    spdlog::debug("[RBLive] log-file: {} candidate(s), newest='{}'",
                  logs.size(), logs.front().string());

    std::string tail = tailFile(logs.front());
    if (tail.empty()) return std::nullopt;

    // Scan the tail backwards for the most recent track-load line.
    auto findLast = [&](const std::string& needle) -> size_t {
        return tail.rfind(needle);
    };

    std::string artist, title;
    size_t pos = findLast("TRACK_LOAD");
    if (pos != std::string::npos) {
        auto lineEnd = tail.find('\n', pos);
        std::string line = tail.substr(pos, lineEnd == std::string::npos ? std::string::npos : lineEnd - pos);
        auto grab = [&](const char* tag) -> std::string {
            auto k = line.find(tag);
            if (k == std::string::npos) return {};
            k = line.find('"', k);
            if (k == std::string::npos) return {};
            auto e = line.find('"', k + 1);
            if (e == std::string::npos) return {};
            return line.substr(k + 1, e - k - 1);
        };
        title  = grab("title=");
        artist = grab("artist=");
    }

    if (title.empty()) {
        pos = findLast("Loaded track");
        if (pos != std::string::npos) {
            auto lineEnd = tail.find('\n', pos);
            std::string line = tail.substr(pos, lineEnd == std::string::npos ? std::string::npos : lineEnd - pos);
            auto q1 = line.find('\'');
            auto q2 = q1 == std::string::npos ? std::string::npos : line.find('\'', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos) {
                std::string inside = line.substr(q1 + 1, q2 - q1 - 1);
                auto sep = inside.find(" - ");
                if (sep != std::string::npos) {
                    artist = inside.substr(0, sep);
                    title  = inside.substr(sep + 3);
                } else {
                    title = inside;
                }
            }
        }
    }

    if (title.empty()) {
        spdlog::debug("[RBLive] log-file: no load-track line in tail of '{}'",
                      logs.front().string());
        return std::nullopt;
    }

    PlayedTrack pt;
    pt.source = "Rekordbox";
    pt.title  = title;
    pt.artist = artist;
    pt.playedAtUnix = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    std::string sig = artist + "||" + title;
    if (sig != lastSignature_) {
        spdlog::info("[RBLive] log-file hit: artist='{}' title='{}' (src='{}')",
                     artist, title, logs.front().string());
        lastSignature_ = sig;
    }
    return pt;
}

std::optional<PlayedTrack> RekordboxLiveWatcher::readNowPlaying()
{
    spdlog::debug("[RBLive] readNowPlaying(): starting window-title + log-file probe");
    if (auto pt = readFromWindowTitle()) return pt;
    if (auto pt = readFromLogFile())     return pt;
    spdlog::debug("[RBLive] readNowPlaying(): no signal from any technique");
    return std::nullopt;
}

} // namespace BeatMate::Services::Rekordbox
