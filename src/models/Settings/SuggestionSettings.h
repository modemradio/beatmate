#pragma once

#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models::Settings {

struct SuggestionSettings {
    bool enableSuggestions = true;
    int maxResults = 20;

    bool useBPM = true;
    bool useKey = true;
    bool useEnergy = true;
    bool useGenre = true;
    bool useMood = false;
    bool useDanceability = false;
    bool useRating = false;
    bool usePlayHistory = false;

    float bpmWeight = 0.3f;
    float keyWeight = 0.3f;
    float energyWeight = 0.2f;
    float genreWeight = 0.1f;
    float moodWeight = 0.05f;
    float danceabilityWeight = 0.05f;

    double bpmTolerance = 6.0;          // +/- BPM
    float energyTolerance = 2.0f;       // +/- energy levels
    bool allowHalfDoubleBPM = true;     // allow 60 BPM to match 120 BPM

    bool onlyCompatibleKeys = true;
    bool includeRelativeKeys = true;    // include relative major/minor
    bool includeParallelKeys = true;    // include parallel major/minor
    bool includeAdjacentKeys = true;    // adjacent on Camelot wheel

    std::string energyPreference = "gradual"; // "gradual", "maintain", "any"
    float maxEnergyJump = 3.0f;         // maximum energy difference

    bool showReasons = true;
    bool showScores = true;
    bool highlightBestMatch = true;
    bool autoRefresh = true;

    bool useMLRecommendations = false;
    bool learnFromPlayHistory = false;

    bool excludeRecentlyPlayed = true;
    int excludeRecentMinutes = 120;     // exclude tracks played in last N minutes
    bool excludeCurrentPlaylist = false;

    SuggestionSettings() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SuggestionSettings,
        enableSuggestions, maxResults,
        useBPM, useKey, useEnergy, useGenre, useMood, useDanceability,
        useRating, usePlayHistory,
        bpmWeight, keyWeight, energyWeight, genreWeight, moodWeight, danceabilityWeight,
        bpmTolerance, energyTolerance, allowHalfDoubleBPM,
        onlyCompatibleKeys, includeRelativeKeys, includeParallelKeys, includeAdjacentKeys,
        energyPreference, maxEnergyJump,
        showReasons, showScores, highlightBestMatch, autoRefresh,
        useMLRecommendations, learnFromPlayHistory,
        excludeRecentlyPlayed, excludeRecentMinutes, excludeCurrentPlaylist
    )
};

} // namespace BeatMate::Models::Settings
