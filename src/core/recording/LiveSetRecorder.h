#pragma once
#include "MixRecorder.h"
#include <chrono>
#include <mutex>
#include <string>
#include <vector>
namespace BeatMate::Core {

struct SetMarker {
    double time;
    std::string trackName;
    std::string type;           // "track", "cue", "loop", "transition", "drop", "custom"
    double bpm = 0.0;
    std::string key;
    std::string notes;
    int deckIndex = -1;
};

class LiveSetRecorder : public MixRecorder {
public:
    LiveSetRecorder() = default;

    void addMarker(const std::string& trackName, const std::string& type = "track");

    void addMarkerWithMetadata(const std::string& trackName, const std::string& type,
                                double bpm, const std::string& key, int deckIndex = -1);

    void onTrackLoaded(int deckIndex, const std::string& trackName, double bpm, const std::string& key);
    void onTransitionStart(int fromDeck, int toDeck);
    void onTransitionEnd(int toDeck);
    void onCueTriggered(int deckIndex, int cueIndex, const std::string& trackName);
    void onLoopActivated(int deckIndex, double loopStart, double loopLength);
    void onDropDetected(int deckIndex, const std::string& trackName);

    std::string generateTimestamp() const;
    std::string generateTimecodeString(double seconds) const;

    std::vector<SetMarker> getMarkers() const;
    std::vector<SetMarker> getMarkersByType(const std::string& type) const;
    std::vector<SetMarker> getMarkersByDeck(int deckIndex) const;
    int getMarkerCount() const;

    void clearMarkers();

    std::string exportToCueSheet() const;
    std::string exportToJson() const;
    bool exportToFile(const std::string& path, const std::string& format = "json") const;

    void setSetName(const std::string& name) { setName_ = name; }
    void setVenueName(const std::string& venue) { venueName_ = venue; }
    std::string getSetName() const { return setName_; }

private:
    mutable std::mutex markersMutex_;
    std::vector<SetMarker> markers_;
    std::string setName_;
    std::string venueName_;
    int autoMarkerCounter_ = 0;
};
} // namespace BeatMate::Core
