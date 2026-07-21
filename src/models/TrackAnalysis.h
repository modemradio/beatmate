#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

struct Section {
    std::string type;
    double startTime = 0.0;
    double endTime = 0.0;
    std::string label;

    Section() = default;
    Section(const std::string& type, double startTime, double endTime, const std::string& label = "")
        : type(type), startTime(startTime), endTime(endTime), label(label) {}

    bool operator==(const Section& other) const {
        return type == other.type && startTime == other.startTime && endTime == other.endTime;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Section, type, startTime, endTime, label)
};

struct Phrase {
    double startTime = 0.0;
    double endTime = 0.0;
    int barCount = 0;
    std::string label;
    float energy = 0.0f;

    Phrase() = default;
    Phrase(double startTime, double endTime, int barCount)
        : startTime(startTime), endTime(endTime), barCount(barCount) {}

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Phrase, startTime, endTime, barCount, label, energy)
};

struct EnergySegment {
    double startTime = 0.0;
    double endTime = 0.0;
    int energy = 1;

    EnergySegment() = default;
    EnergySegment(double startTime, double endTime, int energy)
        : startTime(startTime), endTime(endTime), energy(energy) {}

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(EnergySegment, startTime, endTime, energy)
};

struct TrackAnalysis {
    int64_t trackId = 0;

    double bpm = 0.0;
    float bpmConfidence = 0.0f;

    std::string key;
    float keyConfidence = 0.0f;

    float energy = 0.0f;
    float loudness = 0.0f;
    float peakLevel = 0.0f;
    float loudnessRange = 0.0f;
    std::string mood;

    float danceability = 0.0f;
    float valence = 0.0f;
    float speechiness = 0.0f;
    float instrumentalness = 0.0f;
    float acousticness = 0.0f;

    double startSilence = 0.0;
    double endSilence = 0.0;

    std::vector<double> beatPositions;
    std::vector<double> downbeats;

    std::vector<Section> sections;
    std::vector<Phrase> phrases;
    std::vector<EnergySegment> energySegments;

    std::vector<double> onsets;

    float spectralCentroid = 0.0f;
    float spectralRolloff = 0.0f;
    float zeroCrossingRate = 0.0f;

    std::vector<std::vector<float>> chromagram;

    int64_t analyzedAt = 0;

    TrackAnalysis() = default;
    explicit TrackAnalysis(int64_t trackId) : trackId(trackId) {}

    bool operator==(const TrackAnalysis& other) const { return trackId == other.trackId; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(TrackAnalysis,
        trackId, bpm, bpmConfidence, key, keyConfidence,
        energy, loudness, peakLevel, loudnessRange, mood,
        danceability, valence, speechiness, instrumentalness, acousticness,
        startSilence, endSilence,
        beatPositions, downbeats,
        sections, phrases, energySegments, onsets,
        spectralCentroid, spectralRolloff, zeroCrossingRate,
        chromagram, analyzedAt
    )
};

}
