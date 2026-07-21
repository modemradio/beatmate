#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include <mutex>
#include <cstdint>

#include <spdlog/spdlog.h>

#include "../../models/Track.h"

namespace BeatMate::Services::Library {

class TrackDatabase;

struct TrackSuggestion {
    Models::Track track;
    float score = 0.0f;          // 0.0 - 1.0
    std::string reason;
    std::vector<std::string> tags;
};

struct SuggestionCriteria {
    std::vector<int64_t> referenceTrackIds;

    std::optional<double> targetBPM;
    double bpmTolerance = 5.0;     // +/- BPM

    std::optional<std::string> targetKey;
    bool allowCompatibleKeys = true;

    std::optional<float> targetEnergy;
    float energyTolerance = 2.0f;

    std::optional<std::string> preferredGenre;
    bool allowSimilarGenres = true;

    std::optional<std::string> targetMood;

    std::vector<int64_t> excludeTrackIds;
    bool excludeRecentlyPlayed = false;
    int recentlyPlayedDays = 7;

    int maxSuggestions = 20;
    float minimumScore = 0.3f;
};

struct PlaylistProfile {
    double averageBPM = 0.0;
    double bpmRange = 0.0;         // max - min
    float averageEnergy = 0.0f;
    std::string dominantGenre;
    std::string dominantKey;
    std::string dominantMood;
    double totalDuration = 0.0;     // seconds
    int trackCount = 0;
    std::map<std::string, int> genreDistribution;
    std::map<std::string, int> keyDistribution;
};

class PlaylistSuggestionService {
public:
    explicit PlaylistSuggestionService(std::shared_ptr<TrackDatabase> database);
    ~PlaylistSuggestionService() = default;

    std::vector<TrackSuggestion> suggestForPlaylist(const std::vector<int64_t>& playlistTrackIds,
                                                     const SuggestionCriteria& criteria = {});

    std::vector<TrackSuggestion> suggestNextTrack(int64_t currentTrackId, int maxSuggestions = 10);

    std::vector<TrackSuggestion> suggestSimilar(int64_t trackId, int maxSuggestions = 20);

    std::vector<TrackSuggestion> suggestByBPMProgression(double startBPM, double endBPM,
                                                           int numTracks = 10);

    std::vector<TrackSuggestion> suggestByEnergyCurve(const std::vector<float>& energyCurve,
                                                        int tracksPerSegment = 3);

    std::vector<TrackSuggestion> suggestForGenreMix(const std::string& genre, int count = 20);
    std::vector<TrackSuggestion> suggestForMoodMix(const std::string& mood, int count = 20);

    PlaylistProfile analyzePlaylist(const std::vector<int64_t>& trackIds);

    std::vector<TrackSuggestion> suggestFillToDuration(const std::vector<int64_t>& currentTrackIds,
                                                         double targetDurationMinutes);

private:
    float calculateSimilarityScore(const Models::Track& reference, const Models::Track& candidate) const;
    float calculateBPMCompatibility(double bpm1, double bpm2) const;
    float calculateKeyCompatibility(const std::string& key1, const std::string& key2) const;
    float calculateEnergyCompatibility(float e1, float e2) const;
    float calculateGenreCompatibility(const std::string& g1, const std::string& g2) const;
    bool isKeyCompatible(const std::string& key1, const std::string& key2) const;

    std::shared_ptr<TrackDatabase> database_;
    mutable std::mutex mutex_;
};

} // namespace BeatMate::Services::Library
