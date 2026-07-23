#include "RkbxLinkSubprocess.h"

#include <juce_core/juce_core.h>
#include <spdlog/spdlog.h>

#include <chrono>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#endif

namespace BeatMate::Services::Rekordbox {

using BeatMate::Services::DJSoftware::PlayedTrack;

namespace {
static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static juce::File locateRkbxBinary()
{
    auto base = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                    .getParentDirectory();
#ifdef _WIN32
    const char* candidates[] = {
        "rkbx_link.exe",
        "rkbx_link/rkbx_link.exe",
        "tools/rkbx_link.exe",
    };
#else
    const char* candidates[] = {
        "rkbx_link",
        "rkbx_link/rkbx_link",
        "tools/rkbx_link",
        "tools/external/rkbx_link",
        "tools/external/rkbx_link/rkbx_link",
        "../Resources/rkbx_link",
        "../Resources/tools/external/rkbx_link",
    };
#endif
    for (auto* rel : candidates) {
        auto f = base.getChildFile(rel);
        if (f.existsAsFile()) return f;
    }
    return {};
}

// Format impose par rkbx_link : lignes `cle valeur`.
static bool writeRkbxConfig(const juce::File& binary, int oscPort)
{
    auto cfg = binary.getSiblingFile("config");
    std::string body =
        "keeper.rekordbox_version 7.2.13\n"
        "outputs osc_track,osc_beat\n"
        "osc_track.addr 127.0.0.1\n"
        "osc_track.port " + std::to_string(oscPort) + "\n"
        "osc_beat.addr 127.0.0.1\n"
        "osc_beat.port " + std::to_string(oscPort + 1) + "\n"
        "log_level info\n";
    return cfg.replaceWithText(juce::String(body));
}
} // namespace

RkbxLinkSubprocess::RkbxLinkSubprocess() = default;
RkbxLinkSubprocess::~RkbxLinkSubprocess() { stop(); }

bool RkbxLinkSubprocess::start(int oscPort)
{
    if (started_) return true;

    // Recepteur OSC monte meme sans binaire : l'utilisateur peut lancer rkbx_link a la main.
    if (!connect(oscPort)) {
        spdlog::warn("[RkbxLink] OSC receiver failed to bind port {}", oscPort);
        return false;
    }
    addListener(this);
    started_ = true;

    // Echec de lancement non fatal : l'OSC reste utilisable.
    auto bin = locateRkbxBinary();
    if (bin.existsAsFile() && writeRkbxConfig(bin, oscPort)) {
#ifdef _WIN32
        STARTUPINFOW si{}; si.cb = sizeof(si);
        si.dwFlags     = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        auto cmd = bin.getFullPathName().toWideCharPointer();
        std::wstring cmdLine(cmd);
        if (CreateProcessW(cmd, cmdLine.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr,
                           bin.getParentDirectory().getFullPathName().toWideCharPointer(),
                           &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            childUp_  = true;
            childTag_ = bin.getFullPathName().toStdString();
            spdlog::info("[RkbxLink] spawned {}", childTag_);
        } else {
            spdlog::warn("[RkbxLink] CreateProcessW failed (err={})", GetLastError());
        }
#elif defined(__APPLE__)
        // rkbx_link reads its `config` from the current working directory, so
        // launch it via a shell that cd's into the binary's folder first.
        auto dir = bin.getParentDirectory().getFullPathName();
        juce::String shellCmd = "cd " + dir.quoted()
                              + " && exec " + bin.getFullPathName().quoted()
                              + " >/dev/null 2>&1";
        juce::StringArray args;
        args.add("/bin/sh");
        args.add("-c");
        args.add(shellCmd);

        juce::ChildProcess child;
        if (child.start(args, 0)) {
            childUp_  = true;
            childTag_ = bin.getFullPathName().toStdString();
            spdlog::info("[RkbxLink] spawned {}", childTag_);
        } else {
            spdlog::warn("[RkbxLink] ChildProcess failed to start {}",
                         bin.getFullPathName().toStdString());
        }
#endif
    } else {
        spdlog::info("[RkbxLink] binary not bundled — OSC receiver up on {} "
                     "but no child process. Run rkbx_link.exe manually "
                     "targeting 127.0.0.1:{} to enable instant Rekordbox.",
                     oscPort, oscPort);
    }
    return true;
}

void RkbxLinkSubprocess::stop()
{
    if (!started_) return;
    removeListener(this);
    disconnect();
    started_ = false;
}

bool RkbxLinkSubprocess::isHealthy() const
{
    int64_t t = lastPacketMs_.load();
    return t != 0 && (nowMs() - t) < 3000;
}

void RkbxLinkSubprocess::oscMessageReceived(const juce::OSCMessage& msg)
{
    // Address: /deck/<N>/<field>   value: arg[0]
    auto addr = msg.getAddressPattern().toString();
    if (!addr.startsWith("/deck/")) return;
    auto rest = addr.substring(6);                // "<N>/<field>"
    auto slash = rest.indexOfChar('/');
    if (slash <= 0) return;
    int deck = rest.substring(0, slash).getIntValue();
    if (deck < 1 || deck > 4) return;
    auto field = rest.substring(slash + 1);

    std::lock_guard<std::mutex> lk(mutex_);
    auto& s = decks_[deck];
    s.lastMs = nowMs();
    lastPacketMs_.store(s.lastMs);

    if (msg.isEmpty()) return;
    const auto& a = msg[0];
    if (field == "title"  && a.isString()) s.title  = a.getString().toStdString();
    else if (field == "artist" && a.isString()) s.artist = a.getString().toStdString();
    else if (field == "bpm"    && a.isFloat32()) s.bpm    = a.getFloat32();
    else if (field == "master" && a.isInt32())  s.master = a.getInt32() != 0;
    else if (field == "playing" && a.isInt32()) s.playing = a.getInt32() != 0;
}

std::optional<PlayedTrack> RkbxLinkSubprocess::readNowPlaying()
{
    std::lock_guard<std::mutex> lk(mutex_);

    int pick = 0;
    for (int i = 1; i <= 4; ++i) {
        if (decks_[i].master && !decks_[i].title.empty()) { pick = i; break; }
    }
    if (pick == 0) {
        for (int i = 1; i <= 4; ++i) {
            if (decks_[i].playing && !decks_[i].title.empty()) { pick = i; break; }
        }
    }
    if (pick == 0) return std::nullopt;

    const auto& s = decks_[pick];
    if (nowMs() - s.lastMs > 5000) return std::nullopt;

    // Anti-doublon : on ne reemet que sur changement reel de morceau.
    std::string key = s.title + "|" + s.artist;
    if (key == lastEmittedKey_) return std::nullopt;
    lastEmittedKey_ = key;

    PlayedTrack pt;
    pt.source = "Rekordbox";
    pt.title  = s.title;
    pt.artist = s.artist;
    pt.bpm    = s.bpm;
    return pt;
}

} // namespace BeatMate::Services::Rekordbox
