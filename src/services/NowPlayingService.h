#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace BeatMate::Services {

struct NowPlayingInfo {
    int deckIndex = -1;
    std::string trackId;
    std::string title;
    std::string artist;
    std::string album;
    std::string key;
    double bpm = 0.0;
    double positionSeconds = 0.0;
    double durationSeconds = 0.0;
    float volume = 1.0f;
    bool isPlaying = false;
    bool isMaster = false;
    float energy = 0.0f;
    std::string artworkPath;
};

struct NowPlayingState {
    NowPlayingInfo deck1;
    NowPlayingInfo deck2;
    NowPlayingInfo deck3;
    NowPlayingInfo deck4;
    int activeDeckCount = 2;
    int masterDeck = 0;
    double masterBpm = 0.0;
    float crossfaderPosition = 0.5f;
    bool isRecording = false;
    double recordingElapsed = 0.0;
};

class NowPlayingService {
public:
    using StateChangeCallback = std::function<void(const NowPlayingState&)>;

    NowPlayingService();
    ~NowPlayingService() = default;

    NowPlayingState getState() const;
    NowPlayingInfo getDeckInfo(int deckIndex) const;

    void updateDeck(int deckIndex, const NowPlayingInfo& info);
    void updateDeckPosition(int deckIndex, double positionSeconds);
    void updateDeckVolume(int deckIndex, float volume);
    void setDeckPlaying(int deckIndex, bool playing);

    void setMasterDeck(int deckIndex);
    void setMasterBpm(double bpm);
    void setCrossfaderPosition(float position);

    void setRecording(bool recording, double elapsed = 0.0);

    void setActiveDeckCount(int count);

    void addStateChangeListener(StateChangeCallback callback);

    NowPlayingInfo getMasterDeckInfo() const;
    bool isAnyDeckPlaying() const;
    std::string getFormattedPosition(int deckIndex) const;
    std::string getFormattedRemaining(int deckIndex) const;
    float getProgressPercent(int deckIndex) const;

private:
    void notifyListeners();
    NowPlayingInfo& getDeckRef(int deckIndex);
    const NowPlayingInfo& getDeckConstRef(int deckIndex) const;

    NowPlayingState state_;
    std::vector<StateChangeCallback> listeners_;
    mutable std::mutex mutex_;
};

} // namespace BeatMate::Services
