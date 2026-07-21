#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../djsoftware/RekordboxHistoryReader.h"
#include "../djsoftware/enginedj/EngineDJLiveReader.h"
#include "../djsoftware/serato/SeratoDatabase.h"
#include "../djsoftware/traktor/TraktorIcecast.h"
#include "../djsoftware/virtualdj/VirtualDJRemote.h"

namespace BeatMate::Services::Live {

struct NowPlayingTrack {
    std::string source;
    std::string title;
    std::string artist;
    std::string filePath;
    std::string key;
    double bpm = 0.0;
    juce::int64 updatedAtMs = 0;
};

class NowPlayingService : private juce::Thread {
public:
    NowPlayingService();
    ~NowPlayingService() override;

    void start(int intervalMs = 1000);
    void stop();
    void pollNow();
    void setPreferredSource(const std::string& sourceName);

    std::optional<NowPlayingTrack> getCurrent() const;
    std::vector<std::string> runningSoftware() const;
    bool isAnySoftwareRunning() const;

    std::function<void(const NowPlayingTrack&)> onTrackChanged;

    static std::optional<NowPlayingTrack> loadLastPersisted();

private:
    void run() override;
    void pollOnce();

    std::optional<NowPlayingTrack> probeVirtualDJ();
    std::optional<NowPlayingTrack> probeSerato();
    std::optional<NowPlayingTrack> probeRekordbox();
    std::optional<NowPlayingTrack> probeEngineDJ();
    std::optional<NowPlayingTrack> probeTraktor();

    static void persistLast(const NowPlayingTrack& t);
    static juce::File persistFile();

    mutable std::mutex stateMutex_;
    std::optional<NowPlayingTrack> current_;
    std::vector<std::string> running_;
    std::string preferredSource_;
    int intervalMs_ = 1000;
    std::set<std::wstring> cachedProcs_;
    juce::int64 lastProcScanMs_ = 0;

    DJSoftware::RekordboxHistoryReader rekordboxReader_;
    EngineDJ::EngineDJLiveReader engineReader_;
    Serato::SeratoDatabase seratoDb_;
    Traktor::TraktorIcecast traktorIcecast_;
    VirtualDJ::VirtualDJRemote vdjRemote_;

    bool seratoOpened_ = false;
    bool icecastStarted_ = false;
    juce::int64 lastVdjConnectMs_ = 0;
    juce::int64 seratoSessionMtimeMs_ = 0;
    std::optional<NowPlayingTrack> seratoCache_;
    juce::int64 traktorNmlMtimeMs_ = 0;
    std::optional<NowPlayingTrack> traktorCache_;

    JUCE_DECLARE_NON_COPYABLE(NowPlayingService)
};

}
