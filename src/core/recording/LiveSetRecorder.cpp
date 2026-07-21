#include "LiveSetRecorder.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

void LiveSetRecorder::addMarker(const std::string& trackName, const std::string& type) {
    SetMarker marker;
    marker.time = getElapsedSeconds();
    marker.trackName = trackName;
    marker.type = type;
    int counter;
    {
        std::lock_guard<std::mutex> lock(markersMutex_);
        markers_.push_back(marker);
        counter = ++autoMarkerCounter_;
    }
    spdlog::info("LiveSetRecorder: marker #{} '{}' [{}] at {}", counter, trackName, type, generateTimecodeString(marker.time));
}

void LiveSetRecorder::addMarkerWithMetadata(const std::string& trackName, const std::string& type,
                                             double bpm, const std::string& key, int deckIndex) {
    SetMarker marker;
    marker.time = getElapsedSeconds();
    marker.trackName = trackName;
    marker.type = type;
    marker.bpm = bpm;
    marker.key = key;
    marker.deckIndex = deckIndex;
    int counter;
    {
        std::lock_guard<std::mutex> lock(markersMutex_);
        markers_.push_back(marker);
        counter = ++autoMarkerCounter_;
    }
    spdlog::info("LiveSetRecorder: marker #{} '{}' [{}] BPM={:.1f} Key={} at {}",
                 counter, trackName, type, bpm, key, generateTimecodeString(marker.time));
}

void LiveSetRecorder::onTrackLoaded(int deckIndex, const std::string& trackName, double bpm, const std::string& key) {
    addMarkerWithMetadata(trackName, "track", bpm, key, deckIndex);
}

void LiveSetRecorder::onTransitionStart(int fromDeck, int toDeck) {
    SetMarker marker;
    marker.time = getElapsedSeconds();
    marker.trackName = "Transition Deck " + std::to_string(fromDeck + 1) + " -> " + std::to_string(toDeck + 1);
    marker.type = "transition_start";
    marker.deckIndex = toDeck;
    {
        std::lock_guard<std::mutex> lock(markersMutex_);
        markers_.push_back(marker);
    }
    spdlog::info("LiveSetRecorder: transition start D{} -> D{} at {}", fromDeck + 1, toDeck + 1, generateTimecodeString(marker.time));
}

void LiveSetRecorder::onTransitionEnd(int toDeck) {
    SetMarker marker;
    marker.time = getElapsedSeconds();
    marker.trackName = "Transition End Deck " + std::to_string(toDeck + 1);
    marker.type = "transition_end";
    marker.deckIndex = toDeck;
    std::lock_guard<std::mutex> lock(markersMutex_);
    markers_.push_back(marker);
}

void LiveSetRecorder::onCueTriggered(int deckIndex, int cueIndex, const std::string& trackName) {
    SetMarker marker;
    marker.time = getElapsedSeconds();
    marker.trackName = trackName;
    marker.type = "cue";
    marker.deckIndex = deckIndex;
    marker.notes = "Cue #" + std::to_string(cueIndex + 1);
    std::lock_guard<std::mutex> lock(markersMutex_);
    markers_.push_back(marker);
}

void LiveSetRecorder::onLoopActivated(int deckIndex, double loopStart, double loopLength) {
    SetMarker marker;
    marker.time = getElapsedSeconds();
    marker.trackName = "Loop";
    marker.type = "loop";
    marker.deckIndex = deckIndex;
    marker.notes = "Start=" + std::to_string(loopStart) + " Len=" + std::to_string(loopLength);
    std::lock_guard<std::mutex> lock(markersMutex_);
    markers_.push_back(marker);
}

void LiveSetRecorder::onDropDetected(int deckIndex, const std::string& trackName) {
    addMarkerWithMetadata(trackName, "drop", 0.0, "", deckIndex);
}

std::string LiveSetRecorder::generateTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm buf{};
#if defined(_WIN32)
    localtime_s(&buf, &t);
#else
    localtime_r(&t, &buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&buf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string LiveSetRecorder::generateTimecodeString(double seconds) const {
    int h = static_cast<int>(seconds) / 3600;
    int m = (static_cast<int>(seconds) % 3600) / 60;
    int s = static_cast<int>(seconds) % 60;
    int ms = static_cast<int>((seconds - std::floor(seconds)) * 1000);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", h, m, s, ms);
    return buf;
}

std::vector<SetMarker> LiveSetRecorder::getMarkers() const {
    std::lock_guard<std::mutex> lock(markersMutex_);
    return markers_;
}

std::vector<SetMarker> LiveSetRecorder::getMarkersByType(const std::string& type) const {
    std::lock_guard<std::mutex> lock(markersMutex_);
    std::vector<SetMarker> result;
    for (const auto& m : markers_) if (m.type == type) result.push_back(m);
    return result;
}

std::vector<SetMarker> LiveSetRecorder::getMarkersByDeck(int deckIndex) const {
    std::lock_guard<std::mutex> lock(markersMutex_);
    std::vector<SetMarker> result;
    for (const auto& m : markers_) if (m.deckIndex == deckIndex) result.push_back(m);
    return result;
}

int LiveSetRecorder::getMarkerCount() const {
    std::lock_guard<std::mutex> lock(markersMutex_);
    return static_cast<int>(markers_.size());
}

void LiveSetRecorder::clearMarkers() {
    std::lock_guard<std::mutex> lock(markersMutex_);
    markers_.clear();
    autoMarkerCounter_ = 0;
}

std::string LiveSetRecorder::exportToCueSheet() const {
    // Snapshot markers under lock, then format without lock held.
    std::vector<SetMarker> snapshot;
    {
        std::lock_guard<std::mutex> lock(markersMutex_);
        snapshot = markers_;
    }

    std::ostringstream oss;
    oss << "REM Set: " << (setName_.empty() ? "Untitled" : setName_) << "\n";
    if (!venueName_.empty()) oss << "REM Venue: " << venueName_ << "\n";
    oss << "REM Date: " << generateTimestamp() << "\n";
    oss << "PERFORMER \"BeatMate DJ\"\n";
    oss << "TITLE \"" << (setName_.empty() ? "Live Set" : setName_) << "\"\n";
    oss << "FILE \"mix.wav\" WAVE\n";

    int trackNum = 1;
    for (const auto& m : snapshot) {
        if (m.type == "track" || m.type == "drop") {
            int totalFrames = static_cast<int>(m.time * 75.0); // CD frames
            int mins = totalFrames / (60 * 75);
            int secs = (totalFrames / 75) % 60;
            int frames = totalFrames % 75;
            oss << "  TRACK " << std::setw(2) << std::setfill('0') << trackNum << " AUDIO\n";
            oss << "    TITLE \"" << m.trackName << "\"\n";
            if (m.bpm > 0) oss << "    REM BPM " << std::fixed << std::setprecision(1) << m.bpm << "\n";
            if (!m.key.empty()) oss << "    REM KEY " << m.key << "\n";
            oss << "    INDEX 01 " << std::setw(2) << std::setfill('0') << mins << ":"
                << std::setw(2) << std::setfill('0') << secs << ":"
                << std::setw(2) << std::setfill('0') << frames << "\n";
            trackNum++;
        }
    }
    return oss.str();
}

std::string LiveSetRecorder::exportToJson() const {
    std::vector<SetMarker> snapshot;
    {
        std::lock_guard<std::mutex> lock(markersMutex_);
        snapshot = markers_;
    }

    nlohmann::json j;
    j["setName"] = setName_;
    j["venue"] = venueName_;
    j["timestamp"] = generateTimestamp();
    j["duration"] = getElapsedSeconds();
    nlohmann::json markerArr = nlohmann::json::array();
    for (const auto& m : snapshot) {
        markerArr.push_back({
            {"time", m.time}, {"timecode", generateTimecodeString(m.time)},
            {"trackName", m.trackName}, {"type", m.type},
            {"bpm", m.bpm}, {"key", m.key}, {"deck", m.deckIndex}, {"notes", m.notes}
        });
    }
    j["markers"] = markerArr;
    return j.dump(2);
}

bool LiveSetRecorder::exportToFile(const std::string& path, const std::string& format) const {
    std::ofstream file(path);
    if (!file) return false;
    if (format == "cue") file << exportToCueSheet();
    else file << exportToJson();
    int count = getMarkerCount();
    spdlog::info("LiveSetRecorder: exported {} markers to '{}'", count, path);
    return true;
}

} // namespace BeatMate::Core
